#include "ConditionsWindow.hpp"

#include <cmath>
#include <cstdio>
#include <QtNumeric>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QTimer>
#include <QFrame>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QCloseEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QApplication>
#include <QPalette>
#include <QRegularExpression>
#include <QToolTip>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QLocale>
#include <QComboBox>
#include <QCheckBox>
#include <QIcon>
#include <QPixmap>
#include <QSet>
#include <QUrlQuery>
#include <QMenu>
#include <QAction>
#include <QToolButton>
#include <QFile>
#include <QTextStream>
#include <QFontMetricsF>
#include <QPolygonF>

#include "revision_utils.hpp"
#include "Win11Theme.hpp"

#include "moc_ConditionsWindow.cpp"

namespace
{
  double const pi = 3.14159265358979323846;
  double rad (double d) {return d * pi / 180.;}
  double deg (double r) {return r * 180. / pi;}

  QString swpc_base () {return "https://services.swpc.noaa.gov/";}

  // where the sun is overhead right now (degrees)
  void subsolar_point (QDateTime const& utc, double& lat, double& lon)
  {
    double const jd = utc.toMSecsSinceEpoch () / 86400000.0 + 2440587.5;
    double const n = jd - 2451545.0;
    double const L = std::fmod (280.460 + 0.9856474 * n, 360.);
    double const g = rad (std::fmod (357.528 + 0.9856003 * n, 360.));
    double const lambda = rad (L + 1.915 * std::sin (g) + 0.020 * std::sin (2 * g));
    double const eps = rad (23.439 - 0.0000004 * n);
    double const decl = std::asin (std::sin (eps) * std::sin (lambda));
    double const ra = std::atan2 (std::cos (eps) * std::sin (lambda), std::cos (lambda));
    double const gmst = std::fmod (18.697374558 + 24.06570982441908 * n, 24.);
    lat = deg (decl);
    lon = deg (ra) - gmst * 15.;
    while (lon < -180.) lon += 360.;
    while (lon > 180.) lon -= 360.;
  }

  // height of the sun above the horizon at a place (degrees)
  double sun_elevation (QDateTime const& utc, double lat, double lon)
  {
    double slat, slon;
    subsolar_point (utc, slat, slon);
    double const s = std::sin (rad (lat)) * std::sin (rad (slat))
      + std::cos (rad (lat)) * std::cos (rad (slat)) * std::cos (rad (lon - slon));
    return deg (std::asin (qBound (-1., s, 1.)));
  }

  // today's sunrise and sunset (UTC) at a place; invalid when the sun
  // doesn't rise or set
  void sun_times (double lat, double lon, QDateTime& rise, QDateTime& set)
  {
    rise = set = QDateTime {};
    auto const now = QDateTime::currentDateTimeUtc ();
    // look from local midnight (by longitude) through the next 24 hours
    auto start = QDateTime {now.date (), QTime {0, 0}, Qt::UTC}.addSecs (static_cast<int> (-lon / 15. * 3600.));
    if (start.secsTo (now) > 86400) start = start.addDays (1);
    if (start > now) start = start.addDays (-1);
    double prev = sun_elevation (start, lat, lon);
    for (int m = 2; m <= 1440; m += 2)
      {
        auto const t = start.addSecs (m * 60);
        double const e = sun_elevation (t, lat, lon);
        if (prev < -0.833 && e >= -0.833 && !rise.isValid ()) rise = t;
        if (prev >= -0.833 && e < -0.833 && !set.isValid ()) set = t;
        prev = e;
      }
  }

  // a number from a JSON value that may be a number or a quoted number
  double number (QJsonValue const& v)
  {
    if (v.isDouble ()) return v.toDouble ();
    if (v.isString ())
      {
        bool ok;
        double const d = v.toString ().trimmed ().toDouble (&ok);
        if (ok) return d;
      }
    return qQNaN ();
  }

  // NOAA files come as a list of objects, or (older format) a list of
  // lists with the names in the first row; give back a list of objects
  QList<QJsonObject> records (QByteArray const& data)
  {
    QList<QJsonObject> out;
    auto const doc = QJsonDocument::fromJson (data);
    if (doc.isObject ()) {out << doc.object (); return out;}
    if (!doc.isArray ()) return out;
    auto const array = doc.array ();
    QStringList names;
    for (auto const& item : array)
      {
        if (item.isObject ())
          {
            out << item.toObject ();
          }
        else if (item.isArray ())
          {
            auto const row = item.toArray ();
            if (names.isEmpty ())
              {
                for (auto const& n : row) names << n.toString ();
                continue;
              }
            QJsonObject o;
            for (int i = 0; i < row.size () && i < names.size (); ++i) o[names[i]] = row[i];
            out << o;
          }
      }
    return out;
  }

  double field (QJsonObject const& o, QStringList const& names)
  {
    for (auto const& name : names)
      {
        for (auto it = o.begin (); it != o.end (); ++it)
          {
            if (0 == it.key ().compare (name, Qt::CaseInsensitive))
              {
                double const d = number (it.value ());
                if (!qIsNaN (d)) return d;
              }
          }
      }
    return qQNaN ();
  }

  // rough day/night band outlook from solar flux and the K index
  // 0 poor, 1 fair, 2 good
  int band_estimate (int band_group, bool day, double sfi, double k)
  {
    if (qIsNaN (sfi)) return -1;
    int level = 0;
    switch (band_group)
      {
      case 0:                   // 80-40 m
        level = day ? 1 : 2;
        break;
      case 1:                   // 30-20 m
        level = day ? (sfi >= 90 ? 2 : 1) : (sfi >= 150 ? 2 : sfi >= 100 ? 1 : 0);
        break;
      case 2:                   // 17-15 m
        level = day ? (sfi >= 120 ? 2 : sfi >= 90 ? 1 : 0) : (sfi >= 200 ? 1 : 0);
        break;
      case 3:                   // 12-10 m
        level = day ? (sfi >= 150 ? 2 : sfi >= 110 ? 1 : 0) : 0;
        break;
      }
    if (!qIsNaN (k))
      {
        if (k >= 7) level -= 2;
        else if (k >= 5) level -= 1;
      }
    return qBound (0, level, 2);
  }

  QColor level_color (int level)
  {
    switch (level)
      {
      case 2: return QColor {"#3FB950"};
      case 1: return QColor {"#D29922"};
      case 0: return QColor {"#F85149"};
      default: return QColor {"#808080"};
      }
  }

  struct Station
  {
    double lat;
    double lon;
    QDateTime heard;
    int snr;
    bool cq;
    bool to_me;
    bool approx;
    QColor highlight;           // colour of the decode in Band Activity
  };

  // a little round colour chip for check boxes and the legend
  QIcon chip (QColor const& c, bool hollow = false)
  {
    QPixmap pm {14, 14};
    pm.fill (Qt::transparent);
    QPainter p {&pm};
    p.setRenderHint (QPainter::Antialiasing);
    p.setPen (QPen {hollow ? c : QColor {0, 0, 0, 160}, hollow ? 2. : 1.});
    p.setBrush (hollow ? QBrush {Qt::NoBrush} : QBrush {c});
    p.drawEllipse (QRectF {2, 2, 10, 10});
    return QIcon {pm};
  }

  // where a place is from another: angular distance (radians) and
  // initial heading (degrees from north)
  void path_to (double lat1, double lon1, double lat2, double lon2, double& dist, double& bearing)
  {
    double const p1 = rad (lat1), p2 = rad (lat2), dl = rad (lon2 - lon1);
    double const c = std::sin (p1) * std::sin (p2) + std::cos (p1) * std::cos (p2) * std::cos (dl);
    dist = std::acos (qBound (-1., c, 1.));
    bearing = deg (std::atan2 (std::sin (dl) * std::cos (p2),
                               std::cos (p1) * std::sin (p2) - std::sin (p1) * std::cos (p2) * std::cos (dl)));
    if (bearing < 0) bearing += 360.;
  }

  // the place reached going dist radians from a place on a heading
  void destination (double lat, double lon, double bearing, double dist, double& lat2, double& lon2)
  {
    double const p1 = rad (lat), b = rad (bearing);
    double const p2 = std::asin (qBound (-1., std::sin (p1) * std::cos (dist) + std::cos (p1) * std::sin (dist) * std::cos (b), 1.));
    double l2 = rad (lon) + std::atan2 (std::sin (b) * std::sin (dist) * std::cos (p1),
                                        std::cos (dist) - std::sin (p1) * std::sin (p2));
    lat2 = deg (p2);
    lon2 = std::fmod (deg (l2) + 540., 360.) - 180.;
  }

  // six-character Maidenhead locator for a place
  QString latlon_to_grid (double lat, double lon)
  {
    double const x = qBound (0., lon + 180., 359.99999);
    double const y = qBound (0., lat + 90., 179.99999);
    QString g;
    g += QChar ('A' + static_cast<int> (x / 20.));
    g += QChar ('A' + static_cast<int> (y / 10.));
    g += QChar ('0' + static_cast<int> (std::fmod (x, 20.) / 2.));
    g += QChar ('0' + static_cast<int> (std::fmod (y, 10.)));
    g += QChar ('a' + static_cast<int> (std::fmod (x, 2.) * 12.));
    g += QChar ('a' + static_cast<int> (std::fmod (y, 1.) * 24.));
    return g;
  }

  QString latlon_text (double lat, double lon)
  {
    return QString {"%1%2%3 %4%5%6"}
      .arg (std::abs (lat), 0, 'f', 1).arg (QChar {0x00B0}).arg (lat >= 0 ? 'N' : 'S')
      .arg (std::abs (lon), 0, 'f', 1).arg (QChar {0x00B0}).arg (lon >= 0 ? 'E' : 'W');
  }

  // the main window's style sheet sets the font, so big text has to be
  // asked for in a style sheet too
  QString big_css (double factor)
  {
    double base = QApplication::font ().pointSizeF ();
    if (base <= 0) base = 9.;
    return QString {"font-size: %1pt; font-weight: bold;"}.arg (base * factor, 0, 'f', 1);
  }

  // call signs are shown with a slashed zero, like a QSL card
  QString slashed (QString call)
  {
    return call.replace ('0', QChar {0x00D8});
  }

  // X-ray class (A, B, C, M, X) and a colour level for a flux in W/m2
  QString xray_class (double flux, int * level = nullptr)
  {
    char cls = 'A';
    double base = 1e-8;
    if (flux >= 1e-4) {cls = 'X'; base = 1e-4;}
    else if (flux >= 1e-5) {cls = 'M'; base = 1e-5;}
    else if (flux >= 1e-6) {cls = 'C'; base = 1e-6;}
    else if (flux >= 1e-7) {cls = 'B'; base = 1e-7;}
    if (level) *level = cls == 'X' ? 0 : cls == 'M' ? 1 : 2;
    return QString {"%1%2"}.arg (cls).arg (flux / base, 0, 'f', 1);
  }

  // the NCDXF/IARU beacons, in the order they transmit; each sends for
  // ten seconds on 14.100, then moves up to 18.110 and so on, so the
  // whole cycle takes three minutes
  struct BeaconDef {char const * call; char const * place; char const * grid;};
  BeaconDef const ncdxf[18] = {
    {"4U1UN", "United Nations, New York", "FN30AS"},
    {"VE8AT", "Inuvik, Canada", "CP38GH"},
    {"W6WX", "Mt. Umunhum, California", "CM97BD"},
    {"KH6RS", "Maui, Hawaii", "BL10TS"},
    {"ZL6B", "Masterton, New Zealand", "RE78TW"},
    {"VK6RBP", "Rolystone, Australia", "OF87AV"},
    {"JA2IGY", "Mt. Asama, Japan", "PM84JK"},
    {"RR9O", "Novosibirsk, Russia", "NO14KX"},
    {"VR2B", "Hong Kong", "OL72BG"},
    {"4S7B", "Colombo, Sri Lanka", "MJ96WV"},
    {"ZS6DN", "Pretoria, South Africa", "KG44DC"},
    {"5Z4B", "Kikuyu, Kenya", "KI88KS"},
    {"4X6TU", "Tel Aviv, Israel", "KM72JB"},
    {"OH2B", "Lohja, Finland", "KP20EH"},
    {"CS3B", "Madeira", "IM12JT"},
    {"LU4AA", "Buenos Aires, Argentina", "GF05TJ"},
    {"OA4B", "Lima, Peru", "FH17MW"},
    {"YV5B", "Caracas, Venezuela", "FJ69CC"},
  };
  char const * const beacon_freqs[5] = {"14.100", "18.110", "21.150", "24.930", "28.200"};
  QColor beacon_color (int band)
  {
    static QColor const colors[5] = {QColor {"#4FC3F7"}, QColor {"#81C784"}, QColor {"#FFD54F"},
                                     QColor {"#FF8A65"}, QColor {"#F06292"}};
    return band >= 0 && band < 5 ? colors[band] : QColor {"#9AA4B2"};
  }
  // which beacon is on band b (0 = 14.100) in this ten-second slot
  int beacon_on (int slot, int band) {return ((slot - band) % 18 + 18) % 18;}

  // places for the names shown when you point at the map
  struct City {QString name; double lat; double lon;};
  QVector<City> const& cities ()
  {
    static QVector<City> list;
    if (list.isEmpty ())
      {
        QFile file {":/map_cities.txt"};
        if (file.open (QIODevice::ReadOnly | QIODevice::Text))
          {
            QTextStream in {&file};
            in.setCodec ("UTF-8");
            while (!in.atEnd ())
              {
                auto const line = in.readLine ();
                if (line.startsWith ('#')) continue;
                auto const parts = line.split ('\t');
                if (parts.size () < 5) continue;
                QString name = parts[0];
                if (parts[1].size () && parts[1] != parts[0]) name += ", " + parts[1];
                name += ", " + parts[2];
                list << City {name, parts[3].toDouble (), parts[4].toDouble ()};
              }
          }
      }
    return list;
  }
}

//
// The map. Four looks: satellite pictures of the Earth by day with the
// lights of the cities at night (NASA Blue Marble and Black Marble,
// public domain), the same by day only, terrain with the sea floor
// (NASA Blue Marble with bathymetry), or plain land and borders (Natural
// Earth, public domain) tinted to suit the colour scheme. Drawn flat, or
// centred on you (azimuthal equidistant: straight lines from the middle
// are beam headings, and the distance from the middle is the distance
// from you). Optional overlays: gray line, a lat/long or Maidenhead grid,
// NOAA aurora forecast, NASA rain and snow and the NCDXF beacons.
// Stations heard in the last 30 minutes are drawn in the colours the
// main window highlights them with.
//
class ConditionsMap final
  : public QWidget
{
public:
  enum Style {DayNight = 0, Satellite = 1, Countries = 2, Terrain = 3};
  enum Projection {Flat = 0, Azimuthal = 1};
  enum Grid {NoGrid = 0, LatLong = 1, Maidenhead = 2, MaidenheadFine = 3};
  struct Beacon {QString call; QString place; double lat; double lon; int band;};

  explicit ConditionsMap (QWidget * parent)
    : QWidget {parent}
    , land_ {QImage {":/map_land.png"}.convertToFormat (QImage::Format_Grayscale8)}
    , borders_ {QImage {":/map_borders.png"}.convertToFormat (QImage::Format_Grayscale8)}
  {
    setMinimumSize (360, 180);
    setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking (true);
    for (auto const& b : ncdxf)
      {
        double lat, lon;
        ConditionsWindow::grid_to_latlon (b.grid, lat, lon);
        beacons << Beacon {b.call, b.place, lat, lon, -1};
      }
    retint ();
    update_night ();
  }

  // options
  Style style {DayNight};
  Projection projection {Flat};
  Grid grid {NoGrid};
  bool gray_line {true};
  bool show_aurora {false};
  bool show_rain {false};
  bool show_beacons {false};
  bool show_cq {true};
  bool show_me {true};
  bool show_called {true};
  bool show_others {true};
  bool miles {false};
  QColor cq_color {"#3FB950"};
  QColor my_call_color {"#FF4D4D"};
  QColor tx_color {"#FFD54A"};
  QHash<QString, Station> stations;
  QSet<QString> called;              // stations you transmitted to
  QVector<Beacon> beacons;
  std::function<void (QString const&, Qt::KeyboardModifiers)> on_double_click;

  // something under the stations changed: build the picture again
  void changed ()
  {
    composite_ = scaled_ = projected_ = QImage {};
    grid_key_.clear ();
    update ();
  }

  void set_style (Style s)
  {
    style = s;
    if ((DayNight == style || Satellite == style) && day_.isNull ())
      {
        // load the satellite pictures the first time they're wanted
        day_ = QImage {":/map_day.jpg"}.convertToFormat (QImage::Format_RGB32);
        night_lights_ = QImage {":/map_night.jpg"}.convertToFormat (QImage::Format_RGB32);
        day_small_ = day_.scaled (1024, 512, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        night_small_ = night_lights_.scaled (1024, 512, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      }
    if (Terrain == style && terrain_.isNull ())
      {
        terrain_ = QImage {":/map_terrain.jpg"}.convertToFormat (QImage::Format_RGB32);
      }
    rebuild_base ();
  }

  void retint ()
  {
    bool const dark = Win11Theme::isDark ();
    auto const pal = palette ();
    // dark schemes: land and borders take on the scheme's accent colour
    // (red land in Night Red, green in Digi Green and so on)
    auto mix = [] (QColor const& a, QColor const& b, double f) {
      return QColor::fromRgbF (a.redF () * (1 - f) + b.redF () * f,
                               a.greenF () * (1 - f) + b.greenF () * f,
                               a.blueF () * (1 - f) + b.blueF () * f);
    };
    auto const window = pal.window ().color ();
    auto const accent = pal.highlight ().color ();
    QColor const sea = dark ? window.darker (160) : QColor {"#BFD7EA"};
    QColor const land_color = dark ? mix (window, accent, 0.40) : QColor {"#E8E3C8"};
    QColor const border_color = dark ? mix (window, accent, 0.7) : QColor {"#8A8466"};
    countries_ = QImage {land_.size (), QImage::Format_ARGB32_Premultiplied};
    countries_.fill (sea);
    QPainter p {&countries_};
    p.drawImage (0, 0, tinted (land_, land_color));
    p.drawImage (0, 0, tinted (borders_, border_color));
    border_lines_ = tinted (borders_, Qt::white);
    rebuild_base ();
  }

  void update_night ()
  {
    // 360 x 180: one pixel per degree, smoothed when drawn
    int const W = 360, H = 180;
    night_ = QImage {W, H, QImage::Format_ARGB32_Premultiplied};
    shade_.resize (W * H);
    double slat, slon;
    subsolar_point (QDateTime::currentDateTimeUtc (), slat, slon);
    sun_lat_ = slat;
    sun_lon_ = slon;
    double const sd = std::sin (rad (slat)), cd = std::cos (rad (slat));
    QVector<double> coslon (W);
    for (int x = 0; x < W; ++x) coslon[x] = std::cos (rad (-180. + x + 0.5 - slon));
    for (int y = 0; y < H; ++y)
      {
        double const lat = 90. - y - 0.5;
        double const a = std::sin (rad (lat)) * sd, b = std::cos (rad (lat)) * cd;
        auto * line = reinterpret_cast<QRgb *> (night_.scanLine (y));
        for (int x = 0; x < W; ++x)
          {
            double const el = deg (std::asin (qBound (-1., a + b * coslon[x], 1.)));
            // full darkness 12 degrees below the horizon, fading up to sunrise
            double const f = qBound (0., (0.8 - el) / 12.8, 1.);
            shade_[y * W + x] = f;
            line[x] = qPremultiply (qRgba (0, 0, 20, static_cast<int> (f * 195)));
          }
      }
    rebuild_base ();
  }

  void set_my (double lat, double lon)
  {
    my_lat_ = lat;
    my_lon_ = lon;
    lut_.clear ();                    // the centred map moves with you
    changed ();
  }
  void set_dx (QString const& call, double lat, double lon) {dx_call_ = call; dx_lat_ = lat; dx_lon_ = lon; update ();}

  void set_aurora (QImage const& img) {aurora_ = img; changed ();}
  void set_rain (QImage const& img) {rain_ = img; changed ();}

protected:
  void resizeEvent (QResizeEvent *) override
  {
    scaled_ = projected_ = QImage {};
    grid_key_.clear ();
  }

  void paintEvent (QPaintEvent *) override
  {
    QPainter p {this};
    p.setRenderHint (QPainter::Antialiasing);
    p.fillRect (rect (), palette ().window ());
    auto const r = map_rect ();
    if (Flat == projection)
      {
        if (scaled_.size () != r.size ())
          {
            scaled_ = composite ().scaled (r.size (), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
          }
        p.drawImage (r.topLeft (), scaled_);
      }
    else
      {
        if (projected_.size () != r.size ()) project (r.width ());
        p.drawImage (r.topLeft (), projected_);
      }
    if (grid != NoGrid) draw_grid (p, r);

    // the sun
    auto const sun = to_point (sun_lat_, sun_lon_);
    p.setPen (QPen {QColor {"#FFB000"}, 2});
    p.setBrush (QColor {"#FFD54A"});
    p.drawEllipse (sun, 7, 7);

    if (show_beacons) draw_beacons (p);

    // great circle to the station being worked
    if (!qIsNaN (my_lat_) && !qIsNaN (dx_lat_))
      {
        p.setPen (QPen {my_call_color, 2});
        draw_great_circle (p, my_lat_, my_lon_, dx_lat_, dx_lon_);
      }

    // stations heard in the last 30 minutes, in Band Activity's colours
    auto const now = QDateTime::currentDateTimeUtc ();
    QColor const neutral = Win11Theme::isDark () ? QColor {"#9AA4B2"} : QColor {"#5A6472"};
    for (auto it = stations.cbegin (); it != stations.cend (); ++it)
      {
        auto const& s = it.value ();
        double const age = s.heard.secsTo (now) / 1800.;
        if (age > 1.) continue;
        bool const was_called = called.contains (it.key ());
        QColor c;
        if (s.to_me)
          {
            if (!show_me) continue;
            c = my_call_color;
          }
        else if (was_called)
          {
            if (!show_called) continue;
            c = tx_color;
          }
        else if (s.cq)
          {
            if (!show_cq) continue;
            c = s.highlight.isValid () ? s.highlight : cq_color;
          }
        else
          {
            if (!show_others) continue;
            c = s.highlight.isValid () ? s.highlight : neutral;
          }
        c.setAlphaF (qBound (0.35, 1. - age * 0.65, 1.));
        auto const pt = to_point (s.lat, s.lon);
        double const radius = s.to_me || was_called ? 5.5 : 4.5;
        if (s.approx)
          {
            p.setPen (QPen {c, 2.});
            p.setBrush (Qt::NoBrush);
            p.drawEllipse (pt, radius, radius);
          }
        else
          {
            p.setPen (QPen {QColor {0, 0, 0, 170}, 1});
            p.setBrush (c);
            p.drawEllipse (pt, radius, radius);
          }
      }

    // home
    if (!qIsNaN (my_lat_))
      {
        auto const home = to_point (my_lat_, my_lon_);
        p.setPen (QPen {Qt::white, 2});
        p.setBrush (my_call_color);
        p.drawRect (QRectF {home.x () - 5, home.y () - 5, 10, 10});
      }
    if (!qIsNaN (dx_lat_))
      {
        auto const dx = to_point (dx_lat_, dx_lon_);
        p.setPen (QPen {Qt::white, 2});
        p.setBrush (my_call_color);
        p.drawEllipse (dx, 6, 6);
        auto f = font ();
        f.setBold (true);
        outlined_text (p, dx + QPointF {10, 5}, slashed (dx_call_), f, Qt::white);
      }
    p.setPen (QPen {palette ().mid ().color (), 1});
    p.setBrush (Qt::NoBrush);
    if (Flat == projection) p.drawRect (QRectF {r}.adjusted (0.5, 0.5, -0.5, -0.5));
    else p.drawEllipse (QRectF {r}.adjusted (0.5, 0.5, -0.5, -0.5));
  }

  // the visible station nearest a point, within a few pixels
  QString station_at (QPointF const& pos) const
  {
    auto const now = QDateTime::currentDateTimeUtc ();
    QString best;
    double best_d = 100.;     // 10 pixels
    for (auto it = stations.cbegin (); it != stations.cend (); ++it)
      {
        if (it->heard.secsTo (now) > 1800 || !shown (it.key (), it.value ())) continue;
        auto const pt = to_point (it->lat, it->lon);
        double const d = std::pow (pt.x () - pos.x (), 2) + std::pow (pt.y () - pos.y (), 2);
        if (d < best_d) {best_d = d; best = it.key ();}
      }
    return best;
  }

  int beacon_at (QPointF const& pos) const
  {
    if (!show_beacons) return -1;
    for (int i = 0; i < beacons.size (); ++i)
      {
        auto const pt = to_point (beacons[i].lat, beacons[i].lon);
        if (std::pow (pt.x () - pos.x (), 2) + std::pow (pt.y () - pos.y (), 2) < 81.) return i;
      }
    return -1;
  }

  bool shown (QString const& call, Station const& s) const
  {
    if (s.to_me) return show_me;
    if (called.contains (call)) return show_called;
    if (s.cq) return show_cq;
    return show_others;
  }

  void mouseDoubleClickEvent (QMouseEvent * e) override
  {
    auto const call = station_at (e->pos ());
    if (call.size () && on_double_click) on_double_click (call, e->modifiers ());
  }

  void mouseMoveEvent (QMouseEvent * e) override
  {
    setCursor (station_at (e->pos ()).size () ? Qt::PointingHandCursor : Qt::ArrowCursor);
    // keep the place under the pointer up to date while a tip is showing
    if (QToolTip::isVisible ()) show_tip (e->pos (), e->globalPos ());
    QWidget::mouseMoveEvent (e);
  }

  bool event (QEvent * e) override
  {
    if (e->type () == QEvent::ToolTip)
      {
        auto * help = static_cast<QHelpEvent *> (e);
        show_tip (help->pos (), help->globalPos ());
        return true;
      }
    return QWidget::event (e);
  }

  void show_tip (QPoint const& pos, QPoint const& global)
  {
    auto const call = station_at (pos);
    if (call.size ())
      {
        auto const& s = stations[call];
        QString what;
        if (s.to_me) what = QObject::tr ("calling you");
        else if (called.contains (call)) what = QObject::tr ("you called");
        else if (s.cq) what = QObject::tr ("calling CQ");
        QToolTip::showText (global,
                            QString {"<b>%1</b> &nbsp;%2 dB &nbsp;%3 UTC &nbsp;%4<br/>%5"}
                            .arg (slashed (call)).arg (s.snr).arg (s.heard.toString ("hh:mm")).arg (what)
                            .arg (QObject::tr ("Double-click to call")), this);
        return;
      }
    int const b = beacon_at (pos);
    if (b >= 0)
      {
        auto const& beacon = beacons[b];
        QString text = QString {"<b>%1</b> &nbsp;%2<br/>"}.arg (slashed (beacon.call)).arg (beacon.place);
        if (beacon.band >= 0) text += QObject::tr ("Sending on %1 MHz now").arg (beacon_freqs[beacon.band]);
        else text += QObject::tr ("NCDXF beacon, not sending this moment");
        QToolTip::showText (global, text, this);
        return;
      }
    double lat, lon;
    if (!from_point (pos, lat, lon))
      {
        QToolTip::hideText ();
        return;
      }
    QString text;
    // the nearest city within a few pixels
    double best_d = 144.;
    for (auto const& c : cities ())
      {
        auto const pt = to_point (c.lat, c.lon);
        double const d = std::pow (pt.x () - pos.x (), 2) + std::pow (pt.y () - pos.y (), 2);
        if (d < best_d) {best_d = d; text = "<b>" + c.name.toHtmlEscaped () + "</b><br/>";}
      }
    text += QObject::tr ("Grid %1 &nbsp;%2").arg (latlon_to_grid (lat, lon)).arg (latlon_text (lat, lon));
    if (!qIsNaN (my_lat_))
      {
        double dist, bearing;
        path_to (my_lat_, my_lon_, lat, lon, dist, bearing);
        double const km = dist * 6371.;
        text += "<br/>" + QObject::tr ("%1 %2 from you, heading %3%4")
          .arg (QLocale {}.toString (qRound (miles ? km * 0.621371 : km)))
          .arg (miles ? QObject::tr ("miles") : QObject::tr ("km"))
          .arg (qRound (bearing)).arg (QChar {0x00B0});
      }
    QToolTip::showText (global, text, this);
  }

private:
  void rebuild_base ()
  {
    switch (style)
      {
      case Countries:
        base_ = countries_;
        break;

      case Terrain:
        if (terrain_.isNull ()) {base_ = countries_; break;}
        base_ = terrain_.convertToFormat (QImage::Format_ARGB32_Premultiplied);
        {
          QPainter p {&base_};
          p.setOpacity (0.35);
          p.drawImage (base_.rect (), border_lines_);
        }
        break;

      case Satellite:
        if (day_.isNull ()) {base_ = countries_; break;}
        base_ = day_.convertToFormat (QImage::Format_ARGB32_Premultiplied);
        {
          QPainter p {&base_};
          p.setOpacity (0.45);
          p.drawImage (base_.rect (), border_lines_);
        }
        break;

      case DayNight:
        {
          if (day_small_.isNull () || shade_.isEmpty ()) {base_ = countries_; break;}
          // blend the day picture into the city lights across the gray line
          base_ = QImage {day_small_.size (), QImage::Format_RGB32};
          int const W = base_.width (), H = base_.height ();
          for (int y = 0; y < H; ++y)
            {
              auto const * d = reinterpret_cast<QRgb const *> (day_small_.constScanLine (y));
              auto const * n = reinterpret_cast<QRgb const *> (night_small_.constScanLine (y));
              auto * out = reinterpret_cast<QRgb *> (base_.scanLine (y));
              int const sy = qMin (179, y * 180 / H);
              for (int x = 0; x < W; ++x)
                {
                  double const f = shade_[sy * 360 + qMin (359, x * 360 / W)];
                  out[x] = qRgb (static_cast<int> (qRed (d[x]) * (1 - f) + qRed (n[x]) * f),
                                 static_cast<int> (qGreen (d[x]) * (1 - f) + qGreen (n[x]) * f),
                                 static_cast<int> (qBlue (d[x]) * (1 - f) + qBlue (n[x]) * f));
                }
            }
          base_ = base_.convertToFormat (QImage::Format_ARGB32_Premultiplied);
          QPainter p {&base_};
          p.setRenderHint (QPainter::SmoothPixmapTransform);
          p.setOpacity (0.4);
          p.drawImage (base_.rect (), border_lines_);
        }
        break;
      }
    changed ();
  }

  // the flat picture with the shading, rain and aurora on it
  QImage const& composite ()
  {
    if (composite_.isNull ())
      {
        composite_ = base_.convertToFormat (QImage::Format_ARGB32_Premultiplied);
        if (composite_.constBits () == base_.constBits ()) composite_ = base_.copy ();
        QPainter p {&composite_};
        p.setRenderHint (QPainter::SmoothPixmapTransform);
        auto const all = composite_.rect ();
        // the day/night picture already shows the night side
        if (gray_line && style != DayNight) p.drawImage (all, night_);
        if (show_rain && !rain_.isNull ()) p.drawImage (all, rain_);
        if (show_aurora && !aurora_.isNull ()) p.drawImage (all, aurora_);
      }
    return composite_;
  }

  double centre_lat () const {return qIsNaN (my_lat_) ? 0. : my_lat_;}
  double centre_lon () const {return qIsNaN (my_lon_) ? 0. : my_lon_;}

  // the flat picture redrawn centred on you; a table of which source
  // pixel goes where is worked out once for each size
  void project (int size)
  {
    auto const& src = composite ();
    int const W = src.width (), H = src.height ();
    if (lut_.size () != size * size || lut_src_ != src.size ())
      {
        lut_.resize (size * size);
        lut_src_ = src.size ();
        double const p0 = rad (centre_lat ()), l0 = rad (centre_lon ());
        double const sp0 = std::sin (p0), cp0 = std::cos (p0);
        double const half = size / 2.;
        for (int y = 0; y < size; ++y)
          {
            for (int x = 0; x < size; ++x)
              {
                double const dx = (x + 0.5 - half) / half, dy = (y + 0.5 - half) / half;
                double const rho = std::sqrt (dx * dx + dy * dy);
                if (rho > 1.) {lut_[y * size + x] = -1; continue;}
                double const c = rho * pi, az = std::atan2 (dx, -dy);
                double const sc = std::sin (c), cc = std::cos (c);
                double const lat = std::asin (qBound (-1., sp0 * cc + cp0 * sc * std::cos (az), 1.));
                double lon = l0 + std::atan2 (std::sin (az) * sc * cp0, cc - sp0 * std::sin (lat));
                double lon_deg = std::fmod (deg (lon) + 540., 360.) - 180.;
                int const sx = qBound (0, static_cast<int> ((lon_deg + 180.) / 360. * W), W - 1);
                int const sy = qBound (0, static_cast<int> ((90. - deg (lat)) / 180. * H), H - 1);
                lut_[y * size + x] = sy * W + sx;
              }
          }
      }
    projected_ = QImage {size, size, QImage::Format_ARGB32_Premultiplied};
    projected_.fill (Qt::transparent);
    auto const * in = reinterpret_cast<QRgb const *> (src.constBits ());
    for (int y = 0; y < size; ++y)
      {
        auto * out = reinterpret_cast<QRgb *> (projected_.scanLine (y));
        for (int x = 0; x < size; ++x)
          {
            int const i = lut_[y * size + x];
            if (i >= 0) out[x] = in[i];
          }
      }
  }

  QRect map_rect () const
  {
    int w = width (), h = height ();
    if (Azimuthal == projection)
      {
        int const s = qMin (w, h);
        return QRect {(w - s) / 2, (h - s) / 2, s, s};
      }
    // keep the flat map 2:1 and centred
    if (w > 2 * h) w = 2 * h; else h = w / 2;
    return QRect {(width () - w) / 2, (height () - h) / 2, w, h};
  }

public:
  QPointF to_point (double lat, double lon) const
  {
    auto const r = map_rect ();
    if (Flat == projection)
      {
        return {r.left () + (lon + 180.) / 360. * r.width (), r.top () + (90. - lat) / 180. * r.height ()};
      }
    double dist, bearing;
    path_to (centre_lat (), centre_lon (), lat, lon, dist, bearing);
    double const rho = dist / pi * r.width () / 2.;
    return {r.center ().x () + 0.5 + rho * std::sin (rad (bearing)),
            r.center ().y () + 0.5 - rho * std::cos (rad (bearing))};
  }

  bool from_point (QPointF const& pt, double& lat, double& lon) const
  {
    auto const r = map_rect ();
    if (Flat == projection)
      {
        if (!r.contains (pt.toPoint ())) return false;
        lon = (pt.x () - r.left ()) / r.width () * 360. - 180.;
        lat = 90. - (pt.y () - r.top ()) / r.height () * 180.;
        return true;
      }
    double const half = r.width () / 2.;
    double const dx = (pt.x () - r.left () - half) / half, dy = (pt.y () - r.top () - half) / half;
    double const rho = std::sqrt (dx * dx + dy * dy);
    if (rho > 1.) return false;
    destination (centre_lat (), centre_lon (), deg (std::atan2 (dx, -dy)), rho * pi, lat, lon);
    return true;
  }

private:
  // a line through places, broken where it jumps across the map
  void add_line (QPainterPath& path, QVector<QPointF> const& pts) const
  {
    double const jump = map_rect ().width () / (Flat == projection ? 2. : 4.);
    for (int i = 0; i < pts.size (); ++i)
      {
        if (0 == i || std::hypot (pts[i].x () - pts[i - 1].x (), pts[i].y () - pts[i - 1].y ()) > jump) path.moveTo (pts[i]);
        else path.lineTo (pts[i]);
      }
  }

  void meridian (QPainterPath& path, double lon, double step) const
  {
    QVector<QPointF> pts;
    for (double lat = -90.; lat <= 90.001; lat += step) pts << to_point (qBound (-89.9, lat, 89.9), lon);
    add_line (path, pts);
  }

  void parallel (QPainterPath& path, double lat, double step) const
  {
    QVector<QPointF> pts;
    for (double lon = -180.; lon <= 180.001; lon += step) pts << to_point (lat, qBound (-179.99, lon, 179.99));
    add_line (path, pts);
  }

  void draw_grid (QPainter& p, QRect const& r)
  {
    // the lines only change with the size, the view or where you are
    auto const key = QString {"%1/%2/%3x%4/%5,%6"}.arg (grid).arg (projection).arg (r.width ()).arg (r.height ())
      .arg (centre_lat ()).arg (centre_lon ());
    if (key != grid_key_)
      {
        grid_key_ = key;
        grid_major_ = grid_minor_ = QPainterPath {};
        double const step = Flat == projection ? 10. : 2.;
        if (LatLong == grid)
          {
            for (int lon = -165; lon <= 180; lon += 15) meridian (grid_major_, lon, step);
            for (int lat = -75; lat <= 75; lat += 15) parallel (grid_major_, lat, step);
          }
        else
          {
            for (int lon = -160; lon < 180; lon += 20) meridian (grid_major_, lon, step);
            for (int lat = -80; lat < 90; lat += 10) parallel (grid_major_, lat, step);
            if (MaidenheadFine == grid)
              {
                // the squares inside each field: 2 by 1 degrees
                double const fine = Flat == projection ? 90. : 3.;
                for (int lon = -178; lon < 180; lon += 2) if (lon % 20) meridian (grid_minor_, lon, fine);
                for (int lat = -89; lat < 90; ++lat) if (lat % 10) parallel (grid_minor_, lat, Flat == projection ? 180. : 3.);
              }
          }
      }
    p.setBrush (Qt::NoBrush);
    if (!grid_minor_.isEmpty ())
      {
        p.setPen (QPen {QColor {255, 255, 255, 32}, 0.6});
        p.drawPath (grid_minor_);
      }
    p.setPen (QPen {QColor {255, 255, 255, 80}, 1});
    p.drawPath (grid_major_);

    auto f = font ();
    f.setPointSizeF (qMax (6., f.pointSizeF () * 0.8));
    QColor const label {255, 255, 255, 170};
    if (LatLong == grid)
      {
        if (Flat != projection || r.width () < 500) return;
        for (int lat = -60; lat <= 60; lat += 30)
          {
            auto const pt = to_point (lat, -180.);
            outlined_text (p, pt + QPointF {3, -3}, QString {"%1%2"}.arg (lat).arg (QChar {0x00B0}), f, label);
          }
        for (int lon = -150; lon <= 150; lon += 30)
          {
            auto const pt = to_point (-90., lon);
            outlined_text (p, pt + QPointF {3, -4}, QString {"%1%2"}.arg (lon).arg (QChar {0x00B0}), f, label);
          }
        return;
      }
    if (r.width () < 500) return;
    QVector<QRectF> used;
    for (int i = 0; i < 18; ++i)
      {
        for (int j = 0; j < 18; ++j)
          {
            QString field;
            field += QChar ('A' + i);
            field += QChar ('A' + j);
            if (Flat == projection)
              {
                if (r.width () < 600) return;
                auto const pt = to_point (-90. + (j + 1) * 10., -180. + i * 20.);
                outlined_text (p, pt + QPointF {3, f.pointSizeF () + 3}, field, f, label);
              }
            else
              {
                double const lat = -90. + j * 10. + 5., lon = -180. + i * 20. + 10.;
                double dist, bearing;
                path_to (centre_lat (), centre_lon (), lat, lon, dist, bearing);
                if (dist > pi * 0.6) continue;      // too squeezed out near the edge
                auto const pt = to_point (lat, lon);
                QFontMetricsF fm {f};
                QRectF const box {pt.x () - fm.boundingRect (field).width () / 2. - 2, pt.y () - fm.ascent () / 2. - 2,
                    fm.boundingRect (field).width () + 4, fm.ascent () + 4};
                // near the pole the fields crowd together; leave out labels that would overlap
                bool crowded = false;
                for (auto const& other : used) if (other.intersects (box)) {crowded = true; break;}
                if (crowded) continue;
                used << box;
                outlined_text (p, pt - QPointF {fm.boundingRect (field).width () / 2., -fm.ascent () / 2.}, field, f, label);
              }
          }
      }
  }

  void draw_beacons (QPainter& p)
  {
    auto f = font ();
    f.setPointSizeF (qMax (6., f.pointSizeF () * 0.85));
    f.setBold (true);
    for (auto const& b : beacons)
      {
        auto const pt = to_point (b.lat, b.lon);
        QPolygonF tri;
        tri << pt + QPointF {0, -7} << pt + QPointF {6, 4.5} << pt + QPointF {-6, 4.5};
        if (b.band >= 0)
          {
            p.setPen (QPen {QColor {0, 0, 0, 200}, 1});
            p.setBrush (beacon_color (b.band));
            p.drawPolygon (tri);
            outlined_text (p, pt + QPointF {8, 4}, slashed (b.call), f, beacon_color (b.band));
          }
        else
          {
            p.setPen (QPen {QColor {230, 230, 230, 200}, 1.4});
            p.setBrush (QColor {0, 0, 0, 90});
            p.drawPolygon (tri);
          }
      }
  }

  void draw_great_circle (QPainter& p, double lat1, double lon1, double lat2, double lon2)
  {
    double dist, bearing;
    path_to (lat1, lon1, lat2, lon2, dist, bearing);
    if (dist < 1e-6) return;
    QVector<QPointF> pts;
    for (int i = 0; i <= 120; ++i)
      {
        double lat, lon;
        destination (lat1, lon1, bearing, dist * i / 120., lat, lon);
        pts << to_point (lat, lon);
      }
    QPainterPath path;
    add_line (path, pts);
    p.setBrush (Qt::NoBrush);
    p.drawPath (path);
  }

  // text with a dark edge so it reads on any background
  static void outlined_text (QPainter& p, QPointF const& at, QString const& s, QFont const& f, QColor const& color)
  {
    QPainterPath text;
    text.addText (at, f, s);
    p.setPen (QPen {QColor {0, 0, 0, 200}, 3});
    p.setBrush (Qt::NoBrush);
    p.drawPath (text);
    p.setPen (Qt::NoPen);
    p.setBrush (color);
    p.drawPath (text);
  }

  // mask (white = land) to an image of one colour with the mask as alpha
  static QImage tinted (QImage const& mask, QColor const& color)
  {
    QImage out {mask.size (), QImage::Format_ARGB32_Premultiplied};
    out.fill (color);
    out.setAlphaChannel (mask);
    return out;
  }

  QImage land_;
  QImage borders_;
  QImage countries_;
  QImage border_lines_;
  QImage day_;
  QImage night_lights_;
  QImage day_small_;
  QImage night_small_;
  QImage terrain_;
  QImage base_;
  QImage composite_;
  QImage scaled_;
  QImage projected_;
  QVector<int> lut_;
  QSize lut_src_;
  QImage night_;
  QVector<double> shade_;
  QImage aurora_;
  QImage rain_;
  QString grid_key_;
  QPainterPath grid_major_;
  QPainterPath grid_minor_;
  double sun_lat_ {0.};
  double sun_lon_ {0.};
  double my_lat_ {qQNaN ()};
  double my_lon_ {qQNaN ()};
  double dx_lat_ {qQNaN ()};
  double dx_lon_ {qQNaN ()};
  QString dx_call_;
};

//
// Solar X-rays over the last day from the GOES satellites, on a log
// scale with the flare classes marked, like the NOAA plot
//
class XRayGraph final
  : public QWidget
{
public:
  explicit XRayGraph (QWidget * parent)
    : QWidget {parent}
  {
    setMinimumSize (240, 120);
    setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
    setToolTip (QObject::tr ("<p>Solar X-rays from the GOES satellites over the last day (NOAA).</p>"
                             "<p><b>Red:</b> 0.1-0.8 nm, the one flare classes come from. <b>Blue:</b> 0.05-0.4 nm.</p>"
                             "<p>M and X class flares can black out HF on the daylight side of the Earth for minutes to hours.</p>"));
  }

  QVector<QPointF> long_band;          // x: seconds since 1970, y: W/m2
  QVector<QPointF> short_band;

protected:
  void paintEvent (QPaintEvent *) override
  {
    QPainter p {this};
    p.setRenderHint (QPainter::Antialiasing);
    auto const pal = palette ();
    QColor const text = pal.text ().color ();
    QFontMetricsF fm {font ()};
    auto big = font ();
    big.setBold (true);
    big.setPointSizeF (big.pointSizeF () * 1.35);
    QFontMetricsF bfm {big};

    QRectF const plot = QRectF (rect ()).adjusted (2, bfm.height () + 4, -(fm.boundingRect ("X").width () + 8), -(fm.height () + 2));
    if (plot.width () < 20 || plot.height () < 20) return;
    p.fillRect (plot, pal.base ());
    double const lo = -9., hi = -3.;
    auto y_of = [&] (double flux) {
      double const l = std::log10 (qMax (flux, 1e-12));
      return plot.bottom () - (qBound (lo, l, hi) - lo) / (hi - lo) * plot.height ();
    };
    double const now = QDateTime::currentMSecsSinceEpoch () / 1000.;
    double const start = now - 24 * 3600.;
    auto x_of = [&] (double t) {return plot.left () + (t - start) / (now - start) * plot.width ();};

    // flare classes: A from 1e-8, B, C, M and X from 1e-4
    QColor grid = text;
    grid.setAlpha (50);
    char const classes[] = "ABCMX";
    for (int i = 0; i < 5; ++i)
      {
        double const y1 = y_of (std::pow (10., -8 + i)), y2 = y_of (std::pow (10., -7 + i));
        if (i >= 3)
          {
            QColor band = i == 3 ? QColor {255, 170, 0, 30} : QColor {255, 60, 60, 36};
            p.fillRect (QRectF {plot.left (), y2, plot.width (), y1 - y2}, band);
          }
        p.setPen (QPen {grid, 1});
        p.drawLine (QPointF {plot.left (), y1}, QPointF {plot.right (), y1});
        p.setPen (text);
        p.drawText (QPointF {plot.right () + 4, (y1 + y2) / 2. + fm.ascent () / 2. - 1}, QString {QChar (classes[i])});
      }
    // hours back
    p.setPen (QPen {grid, 1});
    for (int h = 6; h < 24; h += 6)
      {
        double const x = x_of (now - h * 3600.);
        p.setPen (QPen {grid, 1});
        p.drawLine (QPointF {x, plot.top ()}, QPointF {x, plot.bottom ()});
        p.setPen (text);
        auto const label = QString {"-%1h"}.arg (h);
        p.drawText (QPointF {x - fm.boundingRect (label).width () / 2., plot.bottom () + fm.ascent () + 1}, label);
      }
    p.drawText (QPointF {plot.right () - fm.boundingRect (QObject::tr ("now")).width (), plot.bottom () + fm.ascent () + 1}, QObject::tr ("now"));

    auto draw = [&] (QVector<QPointF> const& series, QColor const& color) {
      QPainterPath path;
      bool started = false;
      double last_t = 0;
      for (auto const& pt : series)
        {
          if (pt.x () < start || pt.y () <= 0) continue;
          QPointF const at {x_of (pt.x ()), y_of (pt.y ())};
          if (!started || pt.x () - last_t > 900.) path.moveTo (at);
          else path.lineTo (at);
          started = true;
          last_t = pt.x ();
        }
      p.setPen (QPen {color, 1.4});
      p.setBrush (Qt::NoBrush);
      p.drawPath (path);
    };
    p.save ();
    p.setClipRect (plot);
    draw (short_band, QColor {"#4FA3FF"});
    draw (long_band, QColor {"#FF5050"});
    p.restore ();
    p.setPen (QPen {pal.mid ().color (), 1});
    p.drawRect (plot.adjusted (0.5, 0.5, -0.5, -0.5));

    // title and the class now, large
    p.setFont (big);
    p.setPen (text);
    p.drawText (QPointF {2, bfm.ascent ()}, QObject::tr ("X-ray"));
    if (long_band.size ())
      {
        int level;
        auto const cls = xray_class (long_band.last ().y (), &level);
        p.setPen (level_color (level));
        p.drawText (QPointF {plot.right () - bfm.boundingRect (cls).width (), bfm.ascent ()}, cls);
      }
  }
};

//
// The latest picture of the Sun from NASA's Solar Dynamics Observatory
//
class SunImage final
  : public QWidget
{
public:
  explicit SunImage (QWidget * parent)
    : QWidget {parent}
  {
    setMinimumSize (110, 110);
    setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
  }
  QSize sizeHint () const override {return {160, 160};}

  void set_image (QImage const& img) {image_ = img; scaled_ = QImage {}; update ();}
  void set_message (QString const& m) {message_ = m; update ();}

protected:
  void paintEvent (QPaintEvent *) override
  {
    QPainter p {this};
    int const s = qMin (width (), height ());
    QRect const r {(width () - s) / 2, (height () - s) / 2, s, s};
    if (image_.isNull ())
      {
        p.setPen (palette ().text ().color ());
        p.drawText (rect (), Qt::AlignCenter | Qt::TextWordWrap, message_);
        return;
      }
    if (scaled_.size () != r.size ()) scaled_ = image_.scaled (r.size (), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    p.drawImage (r.topLeft (), scaled_);
  }

private:
  QImage image_;
  QImage scaled_;
  QString message_;
};

ConditionsWindow::ConditionsWindow (QSettings * settings, QWidget * parent)
  : QWidget {parent, Qt::Window}
  , settings_ {settings}
  , network_ {new QNetworkAccessManager {this}}
  , refresh_timer_ {new QTimer {this}}
  , clock_timer_ {new QTimer {this}}
  , sfi_ {qQNaN ()}
  , ssn_ {qQNaN ()}
  , kp_ {qQNaN ()}
  , a_index_ {qQNaN ()}
  , wind_ {qQNaN ()}
  , bz_ {qQNaN ()}
  , bt_ {qQNaN ()}
  , xray_ {qQNaN ()}
{
  setObjectName ("ConditionsWindow");
  setWindowTitle (tr ("Conditions - %1").arg (QApplication::applicationName ()));
  sun_timer_ = new QTimer {this};
  sun_timer_->setInterval (15 * 60 * 1000);       // SDO puts up new pictures every 15 minutes or so
  connect (sun_timer_, &QTimer::timeout, this, &ConditionsWindow::fetch_sun);
  build_ui ();

  refresh_timer_->setInterval (15 * 60 * 1000);   // NOAA updates every few minutes; no need to ask more
  connect (refresh_timer_, &QTimer::timeout, this, &ConditionsWindow::refresh);
  clock_timer_->setInterval (1000);
  connect (clock_timer_, &QTimer::timeout, this, &ConditionsWindow::tick);
  overlay_timer_ = new QTimer {this};
  overlay_timer_->setInterval (30 * 60 * 1000);    // aurora and rain: every 30 minutes
  connect (overlay_timer_, &QTimer::timeout, this, &ConditionsWindow::refresh_overlays);

  settings_->beginGroup ("Conditions");
  restoreGeometry (settings_->value ("geometry").toByteArray ());
  settings_->endGroup ();
}

ConditionsWindow::~ConditionsWindow ()
{
}

QFrame * ConditionsWindow::make_box (QString const& key, QString const& title, QVBoxLayout *& body)
{
  auto * frame = new QFrame {this};
  frame->setObjectName ("conditionsBox");
  body = new QVBoxLayout {frame};
  body->setContentsMargins (8, 6, 8, 6);
  body->setSpacing (3);
  if (title.size ())
    {
      auto * label = new QLabel {"<b>" + title + "</b>", frame};
      body->addWidget (label);
    }
  boxes_[key] = frame;
  return frame;
}

void ConditionsWindow::add_box_action (QString const& key, QString const& text)
{
  auto * action = boxes_menu_->addAction (text);
  action->setCheckable (true);
  settings_->beginGroup ("Conditions");
  action->setChecked (settings_->value ("Box_" + key, true).toBool ());
  settings_->endGroup ();
  box_actions_[key] = action;
  boxes_[key]->setVisible (action->isChecked ());
  connect (action, &QAction::toggled, this, [this, key] (bool on) {
      boxes_[key]->setVisible (on);
      settings_->beginGroup ("Conditions");
      settings_->setValue ("Box_" + key, on);
      settings_->endGroup ();
      update_containers ();
      if (on && "sun" == key && isVisible ()) fetch_sun ();
    });
}

bool ConditionsWindow::box_on (QString const& key) const
{
  auto * action = box_actions_.value (key);
  return action && action->isChecked ();
}

void ConditionsWindow::update_containers ()
{
  // hide a row or column when every box in it is switched off
  top_row_->setVisible (box_on ("clock") || box_on ("xray") || box_on ("sun") || box_on ("beacons"));
  left_column_->setVisible (box_on ("de") || box_on ("dx") || box_on ("bands"));
  if (box_on ("sun") && isVisible ()) sun_timer_->start (); else sun_timer_->stop ();
}

void ConditionsWindow::build_ui ()
{
  boxes_menu_ = new QMenu {this};
  auto * outer = new QVBoxLayout {this};
  outer->setContentsMargins (10, 10, 10, 8);
  outer->setSpacing (8);
  QVBoxLayout * body;

  // ---- top row: call and clocks, X-ray graph, the Sun, beacons ----
  top_row_ = new QWidget {this};
  auto * top = new QHBoxLayout {top_row_};
  top->setContentsMargins (0, 0, 0, 0);
  top->setSpacing (8);
  top_row_->setMaximumHeight (210);

  auto * clock_box = make_box ("clock", {}, body);
  call_label_ = new QLabel {clock_box};
  call_label_->setAlignment (Qt::AlignCenter);
  auto f = call_label_->font ();
  f.setPointSizeF (f.pointSizeF () * 2.6);
  f.setBold (true);
  call_label_->setFont (f);
  utc_clock_ = new QLabel {clock_box};
  utc_clock_->setAlignment (Qt::AlignCenter);
  f = utc_clock_->font ();
  f.setPointSizeF (f.pointSizeF () * 2.0);
  f.setBold (true);
  utc_clock_->setFont (f);
  utc_date_ = new QLabel {clock_box};
  utc_date_->setAlignment (Qt::AlignCenter);
  local_clock_ = new QLabel {clock_box};
  local_clock_->setAlignment (Qt::AlignCenter);
  f = local_clock_->font ();
  f.setPointSizeF (f.pointSizeF () * 1.3);
  local_clock_->setFont (f);
  body->addStretch ();
  body->addWidget (call_label_);
  body->addWidget (utc_clock_);
  body->addWidget (utc_date_);
  body->addWidget (local_clock_);
  body->addStretch ();
  top->addWidget (clock_box, 3);

  auto * xray_box = make_box ("xray", {}, body);
  xray_graph_ = new XRayGraph {xray_box};
  body->addWidget (xray_graph_);
  top->addWidget (xray_box, 4);

  auto * sun_box = make_box ("sun", {}, body);
  sun_image_ = new SunImage {sun_box};
  sun_image_->set_message (tr ("The Sun from NASA SDO"));
  sun_image_->setToolTip (tr ("The latest picture of the Sun from NASA's Solar Dynamics Observatory. "
                              "Bright spots in the ultraviolet views are active regions that can flare."));
  sun_channel_ = new QComboBox {sun_box};
  sun_channel_->addItem (tr ("Sunspots (visible)"), "HMIIC");
  sun_channel_->addItem (tr ("Corona 193 %1").arg (QChar {0x00C5}), "0193");
  sun_channel_->addItem (tr ("Corona 171 %1").arg (QChar {0x00C5}), "0171");
  sun_channel_->addItem (tr ("Chromosphere 304 %1").arg (QChar {0x00C5}), "0304");
  sun_channel_->addItem (tr ("Magnetic field"), "HMIB");
  settings_->beginGroup ("Conditions");
  sun_channel_->setCurrentIndex (qBound (0, settings_->value ("SunChannel", 1).toInt (), sun_channel_->count () - 1));
  settings_->endGroup ();
  connect (sun_channel_, static_cast<void (QComboBox::*) (int)> (&QComboBox::currentIndexChanged), this, [this] (int i) {
      settings_->beginGroup ("Conditions");
      settings_->setValue ("SunChannel", i);
      settings_->endGroup ();
      sun_image_->set_image (QImage {});
      sun_image_->set_message (tr ("Loading..."));
      fetch_sun ();
    });
  body->addWidget (sun_image_, 1);
  body->addWidget (sun_channel_);
  top->addWidget (sun_box, 2);

  auto * beacon_box = make_box ("beacons", tr ("NCDXF beacons"), body);
  beacon_box->setToolTip (tr ("<p>The NCDXF/IARU beacons take turns sending for ten seconds on each band, "
                              "at 100 watts. If you can hear one, the band is open to it.</p>"
                              "<p>Switch on <b>Beacons</b> over the map to see where they are.</p>"));
  for (int b = 0; b < 5; ++b)
    {
      beacon_rows_[b] = new QLabel {beacon_box};
      body->addWidget (beacon_rows_[b]);
    }
  beacon_next_ = new QLabel {beacon_box};
  beacon_next_->setEnabled (false);
  body->addWidget (beacon_next_);
  body->addStretch ();
  top->addWidget (beacon_box, 3);
  outer->addWidget (top_row_);

  // ---- space weather tiles ----
  auto * spacewx = new QWidget {this};
  boxes_["spacewx"] = spacewx;
  auto * tiles = new QGridLayout {spacewx};
  tiles->setContentsMargins (0, 0, 0, 0);
  tiles->setSpacing (6);
  struct {char const * key; char const * caption; char const * tip;} const defs[] = {
    {"sfi", QT_TR_NOOP ("Solar flux"), QT_TR_NOOP ("10.7 cm solar flux index (SFI). Higher is better for the high bands.")},
    {"ssn", QT_TR_NOOP ("Sunspots"), QT_TR_NOOP ("Sunspot number reported by NOAA for the last full day.")},
    {"k", QT_TR_NOOP ("K index"), QT_TR_NOOP ("Planetary K index, 0 to 9. Low is quiet; 5 or more is a geomagnetic storm.")},
    {"a", QT_TR_NOOP ("A index"), QT_TR_NOOP ("Planetary A index for the last 24 hours. Under 10 is quiet.")},
    {"xray", QT_TR_NOOP ("X-ray"), QT_TR_NOOP ("Solar X-ray level now. M and X class flares can black out HF on the daylight side.")},
    {"wind", QT_TR_NOOP ("Solar wind"), QT_TR_NOOP ("Solar wind speed in km/s. Fast wind (over 500) can disturb the bands.")},
    {"bz", QT_TR_NOOP ("Bz"), QT_TR_NOOP ("North-south part of the solar magnetic field, in nT. Strongly negative (southward) means storms are more likely.")},
  };
  int col = 0;
  for (auto const& d : defs)
    {
      auto * frame = new QFrame {spacewx};
      frame->setObjectName ("conditionsTile");
      frame->setToolTip (tr (d.tip));
      auto * v = new QVBoxLayout {frame};
      v->setContentsMargins (8, 4, 8, 4);
      v->setSpacing (0);
      Tile t;
      t.caption = new QLabel {tr (d.caption), frame};
      t.caption->setAlignment (Qt::AlignCenter);
      t.value = new QLabel {QString {QChar {0x2014}}, frame};
      t.value->setAlignment (Qt::AlignCenter);
      auto vf = t.value->font ();
      vf.setPointSizeF (vf.pointSizeF () * 1.7);
      vf.setBold (true);
      t.value->setFont (vf);
      t.note = new QLabel {frame};
      t.note->setAlignment (Qt::AlignCenter);
      v->addWidget (t.caption);
      v->addWidget (t.value);
      v->addWidget (t.note);
      tiles_[d.key] = t;
      tiles->addWidget (frame, 0, col++);
    }
  outer->addWidget (spacewx);

  // ---- you, the DX station and the band outlook beside the map ----
  auto * middle = new QHBoxLayout;
  middle->setSpacing (8);
  left_column_ = new QWidget {this};
  auto * side = new QVBoxLayout {left_column_};
  side->setContentsMargins (0, 0, 0, 0);
  side->setSpacing (8);
  left_column_->setFixedWidth (270);

  auto * de_box = make_box ("de", {}, body);
  de_info_ = new QLabel {de_box};
  de_info_->setWordWrap (true);
  de_info_->setTextFormat (Qt::RichText);
  body->addWidget (de_info_);
  side->addWidget (de_box);

  auto * dx_box = make_box ("dx", {}, body);
  dx_info_ = new QLabel {dx_box};
  dx_info_->setWordWrap (true);
  dx_info_->setTextFormat (Qt::RichText);
  body->addWidget (dx_info_);
  side->addWidget (dx_box);

  auto * bands_box = make_box ("bands", tr ("Band outlook"), body);
  bands_box->setToolTip (tr ("A rough estimate from the solar flux and K index, not a forecast for any one path."));
  auto * bands = new QGridLayout;
  bands->setHorizontalSpacing (6);
  bands->setVerticalSpacing (4);
  bands->addWidget (new QLabel {tr ("Day"), bands_box}, 0, 1, Qt::AlignCenter);
  bands->addWidget (new QLabel {tr ("Night"), bands_box}, 0, 2, Qt::AlignCenter);
  char const * groups[] = {"80-40m", "30-20m", "17-15m", "12-10m"};
  for (int g = 0; g < 4; ++g)
    {
      bands->addWidget (new QLabel {groups[g], bands_box}, g + 1, 0);
      for (int dn = 0; dn < 2; ++dn)
        {
          auto * cell = new QLabel {QString {QChar {0x2014}}, bands_box};
          cell->setAlignment (Qt::AlignCenter);
          cell->setMinimumWidth (56);
          band_cells_[g][dn] = cell;
          bands->addWidget (cell, g + 1, dn + 1);
        }
    }
  body->addLayout (bands);
  auto * est = new QLabel {tr ("Estimated from solar flux and K"), bands_box};
  est->setEnabled (false);
  body->addWidget (est);
  side->addWidget (bands_box);
  side->addStretch ();
  middle->addWidget (left_column_);

  map_ = new ConditionsMap {this};
  map_->on_double_click = [this] (QString const& call, Qt::KeyboardModifiers modifiers) {
    Q_EMIT station_double_clicked (call, modifiers);
  };
  auto * map_column = new QVBoxLayout;
  map_column->setSpacing (4);
  map_column->addWidget (build_map_bar ());
  map_column->addWidget (map_, 1);
  middle->addLayout (map_column, 1);
  outer->addLayout (middle, 1);

  status_ = new QLabel {this};
  status_->setEnabled (false);
  status_->setWordWrap (true);
  outer->addWidget (status_);

  // the Boxes menu
  add_box_action ("clock", tr ("Call sign and clock"));
  add_box_action ("xray", tr ("X-ray graph"));
  add_box_action ("sun", tr ("The Sun (NASA SDO)"));
  add_box_action ("beacons", tr ("NCDXF beacons"));
  add_box_action ("spacewx", tr ("Space weather"));
  add_box_action ("de", tr ("You (DE)"));
  add_box_action ("dx", tr ("DX station"));
  add_box_action ("bands", tr ("Band outlook"));
  update_containers ();

  resize (1280, 860);
  apply_colors ();
  update_beacons ();
}

QWidget * ConditionsWindow::build_map_bar ()
{
  auto * bar = new QWidget {this};
  auto * v = new QVBoxLayout {bar};
  v->setContentsMargins (0, 0, 0, 0);
  v->setSpacing (2);

  // first row: which map, how it's drawn, and what to draw over it
  auto * row1 = new QHBoxLayout;
  row1->setSpacing (8);
  row1->addWidget (new QLabel {tr ("Map:"), bar});
  style_box_ = new QComboBox {bar};
  style_box_->addItem (tr ("Satellite, day and night"));
  style_box_->addItem (tr ("Satellite"));
  style_box_->addItem (tr ("Countries"));
  style_box_->addItem (tr ("Terrain"));
  style_box_->setToolTip (tr ("<p><b>Satellite, day and night:</b> the Earth by day, city lights on the night side.</p>"
                              "<p><b>Satellite:</b> the Earth by day everywhere.</p>"
                              "<p><b>Countries:</b> plain land and borders in this colour scheme.</p>"
                              "<p><b>Terrain:</b> mountains and the sea floor.</p>"));
  row1->addWidget (style_box_);
  projection_box_ = new QComboBox {bar};
  projection_box_->addItem (tr ("Flat"));
  projection_box_->addItem (tr ("Centred on you"));
  projection_box_->setToolTip (tr ("<p><b>Flat:</b> the whole world, north up.</p>"
                                   "<p><b>Centred on you:</b> an azimuthal map. A straight line from the middle is the "
                                   "beam heading to that place, and the farther out, the farther away. "
                                   "The edge is the far side of the world.</p>"));
  row1->addWidget (projection_box_);
  row1->addWidget (new QLabel {tr ("Grid:"), bar});
  grid_box_ = new QComboBox {bar};
  grid_box_->addItem (tr ("None"));
  grid_box_->addItem (tr ("Lat/long"));
  grid_box_->addItem (tr ("Maidenhead"));
  grid_box_->addItem (tr ("Maidenhead squares"));
  grid_box_->setToolTip (tr ("<p><b>Lat/long:</b> lines every 15 degrees.</p>"
                             "<p><b>Maidenhead:</b> fields AA to RR.</p>"
                             "<p><b>Maidenhead squares:</b> the fields and the squares inside them.</p>"
                             "<p>Point at the map to see the grid square, the nearest city and how far it is from you.</p>"));
  row1->addWidget (grid_box_);
  gray_box_ = new QCheckBox {tr ("Gray line"), bar};
  gray_box_->setToolTip (tr ("Shade the night side of the Earth"));
  aurora_box_ = new QCheckBox {tr ("Aurora"), bar};
  aurora_box_->setToolTip (tr ("NOAA's 30-minute aurora forecast. Strong aurora can absorb or reflect HF and VHF signals near the poles."));
  rain_box_ = new QCheckBox {tr ("Rain and snow"), bar};
  rain_box_->setToolTip (tr ("Where it's raining or snowing over the last half hour or so, from NASA satellites"));
  beacons_box_ = new QCheckBox {tr ("Beacons"), bar};
  beacons_box_->setToolTip (tr ("The 18 NCDXF/IARU beacons. The ones sending now are filled in the colour of their band."));
  row1->addWidget (gray_box_);
  row1->addWidget (aurora_box_);
  row1->addWidget (rain_box_);
  row1->addWidget (beacons_box_);
  row1->addStretch ();
  auto * boxes = new QToolButton {bar};
  boxes->setText (tr ("Boxes"));
  boxes->setToolTip (tr ("Choose which boxes this window shows"));
  boxes->setPopupMode (QToolButton::InstantPopup);
  boxes->setMenu (boxes_menu_);
  row1->addWidget (boxes);
  v->addLayout (row1);

  // second row: which stations; the check boxes double as the legend
  auto * row2 = new QHBoxLayout;
  row2->setSpacing (10);
  row2->addWidget (new QLabel {tr ("Show:"), bar});
  show_cq_ = new QCheckBox {tr ("Calling CQ"), bar};
  show_me_ = new QCheckBox {tr ("Calling you"), bar};
  show_called_ = new QCheckBox {tr ("You called"), bar};
  show_others_ = new QCheckBox {tr ("Everyone else"), bar};
  show_cq_->setToolTip (tr ("Stations calling CQ, in the colour Band Activity shows them in (new DXCC, new grid and so on)"));
  show_me_->setToolTip (tr ("Stations calling you (the My Call colour in Settings > Colors)"));
  show_called_->setToolTip (tr ("Stations you've transmitted to (the Transmitted Message colour in Settings > Colors)"));
  show_others_->setToolTip (tr ("Everyone else heard in the last 30 minutes"));
  for (auto * box : {show_cq_, show_me_, show_called_, show_others_}) row2->addWidget (box);
  auto * ring = new QLabel {bar};
  ring->setPixmap (chip (palette ().text ().color (), true).pixmap (14, 14));
  row2->addSpacing (8);
  row2->addWidget (ring);
  row2->addWidget (new QLabel {tr ("= placed by country only"), bar});
  row2->addStretch ();
  v->addLayout (row2);

  // restore and react
  settings_->beginGroup ("Conditions");
  style_box_->setCurrentIndex (qBound (0, settings_->value ("MapStyle", 0).toInt (), style_box_->count () - 1));
  projection_box_->setCurrentIndex (qBound (0, settings_->value ("MapProjection", 0).toInt (), 1));
  // older betas had a single Grid squares check box
  int const old_grid = settings_->value ("GridSquares", false).toBool () ? 2 : 0;
  grid_box_->setCurrentIndex (qBound (0, settings_->value ("MapGrid", old_grid).toInt (), grid_box_->count () - 1));
  gray_box_->setChecked (settings_->value ("GrayLine", true).toBool ());
  aurora_box_->setChecked (settings_->value ("Aurora", false).toBool ());
  rain_box_->setChecked (settings_->value ("Rain", false).toBool ());
  beacons_box_->setChecked (settings_->value ("Beacons", false).toBool ());
  show_cq_->setChecked (settings_->value ("ShowCQ", true).toBool ());
  show_me_->setChecked (settings_->value ("ShowCallingMe", true).toBool ());
  show_called_->setChecked (settings_->value ("ShowCalled", true).toBool ());
  show_others_->setChecked (settings_->value ("ShowOthers", true).toBool ());
  settings_->endGroup ();

  auto apply = [this] {
    map_->projection = static_cast<ConditionsMap::Projection> (projection_box_->currentIndex ());
    map_->grid = static_cast<ConditionsMap::Grid> (grid_box_->currentIndex ());
    map_->gray_line = gray_box_->isChecked ();
    map_->show_aurora = aurora_box_->isChecked ();
    map_->show_rain = rain_box_->isChecked ();
    map_->show_beacons = beacons_box_->isChecked ();
    map_->show_cq = show_cq_->isChecked ();
    map_->show_me = show_me_->isChecked ();
    map_->show_called = show_called_->isChecked ();
    map_->show_others = show_others_->isChecked ();
    gray_box_->setEnabled (style_box_->currentIndex () != ConditionsMap::DayNight);
    map_->set_style (static_cast<ConditionsMap::Style> (style_box_->currentIndex ()));   // redraws
    save_map_options ();
  };
  for (auto * combo : {style_box_, projection_box_, grid_box_})
    {
      connect (combo, static_cast<void (QComboBox::*) (int)> (&QComboBox::currentIndexChanged), this, apply);
    }
  for (auto * box : {gray_box_, beacons_box_, show_cq_, show_me_, show_called_, show_others_})
    {
      connect (box, &QCheckBox::toggled, this, apply);
    }
  for (auto * box : {aurora_box_, rain_box_})
    {
      connect (box, &QCheckBox::toggled, this, [this, apply] {
          apply ();
          if (isVisible ()) refresh_overlays ();
        });
    }
  QTimer::singleShot (0, this, apply);
  update_legend ();
  return bar;
}

void ConditionsWindow::save_map_options ()
{
  settings_->beginGroup ("Conditions");
  settings_->setValue ("MapStyle", style_box_->currentIndex ());
  settings_->setValue ("MapProjection", projection_box_->currentIndex ());
  settings_->setValue ("MapGrid", grid_box_->currentIndex ());
  settings_->remove ("GridSquares");
  settings_->setValue ("GrayLine", gray_box_->isChecked ());
  settings_->setValue ("Aurora", aurora_box_->isChecked ());
  settings_->setValue ("Rain", rain_box_->isChecked ());
  settings_->setValue ("Beacons", beacons_box_->isChecked ());
  settings_->setValue ("ShowCQ", show_cq_->isChecked ());
  settings_->setValue ("ShowCallingMe", show_me_->isChecked ());
  settings_->setValue ("ShowCalled", show_called_->isChecked ());
  settings_->setValue ("ShowOthers", show_others_->isChecked ());
  settings_->endGroup ();
}

void ConditionsWindow::update_legend ()
{
  if (!show_cq_) return;
  QColor const neutral = Win11Theme::isDark () ? QColor {"#9AA4B2"} : QColor {"#5A6472"};
  show_cq_->setIcon (chip (cq_color_));
  show_me_->setIcon (chip (my_call_color_));
  show_called_->setIcon (chip (tx_color_));
  show_others_->setIcon (chip (neutral));
}

void ConditionsWindow::set_highlight_colors (QColor const& cq, QColor const& my_call, QColor const& tx)
{
  if (cq.isValid ()) cq_color_ = cq;
  if (my_call.isValid ()) my_call_color_ = my_call;
  if (tx.isValid ()) tx_color_ = tx;
  map_->cq_color = cq_color_;
  map_->my_call_color = my_call_color_;
  map_->tx_color = tx_color_;
  update_legend ();
  map_->update ();
}

void ConditionsWindow::mark_called (QString const& call)
{
  if (call.isEmpty ()) return;
  map_->called.insert (call);
  if (isVisible ()) map_->update ();
}

void ConditionsWindow::refresh_overlays ()
{
  if (aurora_box_->isChecked ())
    {
      QNetworkRequest req {QUrl {swpc_base () + "json/ovation_aurora_latest.json"}};
      req.setRawHeader ("User-Agent", http_user_agent ().toUtf8 ());
      auto * reply = network_->get (req);
      connect (reply, &QNetworkReply::finished, this, [this, reply] {
          reply->deleteLater ();
          if (QNetworkReply::NoError != reply->error ())
            {
              overlay_status_ = tr ("The aurora forecast couldn't be reached.");
              return;
            }
          // [longitude 0-359, latitude -90..90, chance %]
          auto const doc = QJsonDocument::fromJson (reply->readAll ());
          auto const coords = doc.object ().value ("coordinates").toArray ();
          QImage img {360, 181, QImage::Format_ARGB32_Premultiplied};
          img.fill (Qt::transparent);
          for (auto const& v : coords)
            {
              auto const a = v.toArray ();
              if (a.size () < 3) continue;
              int lon = a[0].toInt ();
              int const lat = a[1].toInt ();
              int const chance = a[2].toInt ();
              if (chance < 3) continue;
              if (lon >= 180) lon -= 360;
              int const x = qBound (0, lon + 180, 359), y = qBound (0, 90 - lat, 180);
              int const alpha = qBound (40, chance * 4, 220);
              // green, turning red where it's strongest
              QColor const c = chance >= 50 ? QColor {255, 70, 70} : chance >= 20 ? QColor {255, 220, 60} : QColor {60, 255, 120};
              img.setPixel (x, y, qPremultiply (qRgba (c.red (), c.green (), c.blue (), alpha)));
            }
          map_->set_aurora (img);
          overlay_status_.clear ();
        });
    }
  if (rain_box_->isChecked ())
    {
      // NASA GIBS: rain and snow rate (IMERG), the latest image
      QUrl url {"https://gibs.earthdata.nasa.gov/wms/epsg4326/best/wms.cgi"};
      QUrlQuery q;
      q.addQueryItem ("SERVICE", "WMS");
      q.addQueryItem ("REQUEST", "GetMap");
      q.addQueryItem ("VERSION", "1.1.1");
      q.addQueryItem ("LAYERS", "IMERG_Precipitation_Rate");
      q.addQueryItem ("SRS", "EPSG:4326");
      q.addQueryItem ("BBOX", "-180,-90,180,90");
      q.addQueryItem ("WIDTH", "1024");
      q.addQueryItem ("HEIGHT", "512");
      q.addQueryItem ("FORMAT", "image/png");
      q.addQueryItem ("TRANSPARENT", "TRUE");
      url.setQuery (q);
      QNetworkRequest req {url};
      req.setRawHeader ("User-Agent", http_user_agent ().toUtf8 ());
      auto * reply = network_->get (req);
      connect (reply, &QNetworkReply::finished, this, [this, reply] {
          reply->deleteLater ();
          QImage img;
          if (QNetworkReply::NoError == reply->error ()) img.loadFromData (reply->readAll ());
          if (img.isNull ())
            {
              overlay_status_ = tr ("Rain and snow couldn't be reached.");
              return;
            }
          map_->set_rain (img.convertToFormat (QImage::Format_ARGB32_Premultiplied));
        });
    }
  if (aurora_box_->isChecked () || rain_box_->isChecked ()) overlay_timer_->start ();
  else overlay_timer_->stop ();
}


void ConditionsWindow::apply_colors ()
{
  auto const pal = palette ();
  setStyleSheet (QString {"QFrame#conditionsTile, QFrame#conditionsBox {background-color: %1; border: 1px solid %2; border-radius: 8px;}"}
                 .arg (pal.alternateBase ().color ().name ()).arg (pal.mid ().color ().name ()));
  // the scheme's accent colour, made to stand out from the background
  auto accent = pal.highlight ().color ();
  auto const background = pal.window ().color ();
  for (int i = 0; i < 6 && std::abs (accent.lightness () - background.lightness ()) < 110; ++i)
    {
      accent = background.lightness () < 128 ? accent.lighter (125) : accent.darker (125);
    }
  call_label_->setStyleSheet (big_css (2.8) + QString {" color: %1;"}.arg (accent.name ()));
  utc_clock_->setStyleSheet (big_css (2.0));
  local_clock_->setStyleSheet (big_css (1.2).replace ("bold", "normal"));
  map_->retint ();
  update_legend ();
  update_tiles ();
  update_bands ();
  xray_graph_->update ();
}

void ConditionsWindow::scheme_changed ()
{
  // called by the main window after View > Color Scheme changes
  apply_colors ();
}

void ConditionsWindow::showEvent (QShowEvent * e)
{
  QWidget::showEvent (e);
  clock_timer_->start ();
  refresh_timer_->start ();
  refresh_overlays ();
  tick ();
  map_->update_night ();
  update_containers ();
  // fetch now unless the numbers are recent
  if (!updated_.isValid () || updated_.secsTo (QDateTime::currentDateTimeUtc ()) > 10 * 60) refresh ();
  if (box_on ("sun")) fetch_sun ();
}

void ConditionsWindow::hideEvent (QHideEvent * e)
{
  // nothing to fetch or draw while it's closed
  clock_timer_->stop ();
  refresh_timer_->stop ();
  overlay_timer_->stop ();
  sun_timer_->stop ();
  QWidget::hideEvent (e);
}

void ConditionsWindow::closeEvent (QCloseEvent * e)
{
  settings_->beginGroup ("Conditions");
  settings_->setValue ("geometry", saveGeometry ());
  settings_->endGroup ();
  QWidget::closeEvent (e);
}

void ConditionsWindow::tick ()
{
  auto const now = QDateTime::currentDateTimeUtc ();
  utc_clock_->setText (now.toString ("hh:mm:ss") + " UTC");
  utc_date_->setText (QLocale {}.toString (now.date (), "ddd d MMM yyyy"));
  local_clock_->setText (tr ("Local %1").arg (now.toLocalTime ().toString ("h:mm:ss AP")));
  update_beacons ();
  if (0 == now.time ().second ())
    {
      map_->update_night ();
      // forget stations not heard for half an hour
      for (auto it = map_->stations.begin (); it != map_->stations.end ();)
        {
          if (it->heard.secsTo (now) > 1800) it = map_->stations.erase (it);
          else ++it;
        }
      update_de_dx ();
      xray_graph_->update ();
    }
}

void ConditionsWindow::update_beacons ()
{
  auto const now = QDateTime::currentDateTimeUtc ();
  int const secs = QTime {0, 0}.secsTo (now.time ());
  int const slot = (secs % 180) / 10;
  for (int b = 0; b < 5; ++b)
    {
      auto const& beacon = ncdxf[beacon_on (slot, b)];
      beacon_rows_[b]->setText (QString {"<span style=\"color:%1\">&#9650;</span> %2 &nbsp;<b>%3</b> &nbsp;%4"}
                                .arg (beacon_color (b).name ()).arg (beacon_freqs[b])
                                .arg (slashed (beacon.call)).arg (QString {beacon.place}.section (',', -1).trimmed ()));
    }
  beacon_next_->setText (tr ("Next change in %1 s").arg (10 - secs % 10));
  if (slot != beacon_slot_)
    {
      beacon_slot_ = slot;
      for (int i = 0; i < map_->beacons.size (); ++i) map_->beacons[i].band = -1;
      for (int b = 0; b < 5; ++b) map_->beacons[beacon_on (slot, b)].band = b;
      if (map_->show_beacons) map_->update ();
    }
}

void ConditionsWindow::fetch_sun ()
{
  if (!box_on ("sun")) return;
  auto const channel = sun_channel_->currentData ().toString ();
  QNetworkRequest req {QUrl {QString {"https://sdo.gsfc.nasa.gov/assets/img/latest/latest_512_%1.jpg"}.arg (channel)}};
  req.setRawHeader ("User-Agent", http_user_agent ().toUtf8 ());
#if QT_VERSION >= QT_VERSION_CHECK (5, 9, 0)
  req.setAttribute (QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
#endif
  auto * reply = network_->get (req);
  connect (reply, &QNetworkReply::finished, this, [this, reply, channel] {
      reply->deleteLater ();
      if (channel != sun_channel_->currentData ().toString ()) return;   // changed while loading
      QImage img;
      if (QNetworkReply::NoError == reply->error ()) img.loadFromData (reply->readAll ());
      if (img.isNull ())
        {
          sun_image_->set_message (tr ("The Sun picture couldn't be reached. Will try again in 15 minutes."));
          return;
        }
      sun_image_->set_image (img);
      sun_image_->setToolTip (tr ("The Sun from NASA's Solar Dynamics Observatory, fetched %1 UTC. "
                                  "Bright spots in the ultraviolet views are active regions that can flare.")
                              .arg (QDateTime::currentDateTimeUtc ().toString ("hh:mm")));
    });
  if (isVisible ()) sun_timer_->start ();
}

void ConditionsWindow::refresh ()
{
  if (pending_ > 0) return;
  failed_ = 0;
  auto const swpc = swpc_base ();
  request (swpc + "products/summary/10cm-flux.json", &ConditionsWindow::on_flux);
  request (swpc + "products/noaa-planetary-k-index.json", &ConditionsWindow::on_kindex);
  request (swpc + "products/summary/solar-wind-speed.json", &ConditionsWindow::on_wind);
  request (swpc + "products/summary/solar-wind-mag-field.json", &ConditionsWindow::on_mag);
  request (swpc + "json/goes/primary/xrays-1-day.json", &ConditionsWindow::on_xray);
  request (swpc + "text/daily-solar-indices.txt", &ConditionsWindow::on_daily);
  status_->setText (tr ("Getting the latest space weather from NOAA..."));
}

void ConditionsWindow::request (QString const& url, void (ConditionsWindow::*handler) (QByteArray const&))
{
  QNetworkRequest req {QUrl {url}};
  req.setRawHeader ("User-Agent", http_user_agent ().toUtf8 ());
#if QT_VERSION >= QT_VERSION_CHECK (5, 9, 0)
  req.setAttribute (QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
#endif
  ++pending_;
  auto * reply = network_->get (req);
  connect (reply, &QNetworkReply::finished, this, [this, reply, handler] {
      reply->deleteLater ();
      --pending_;
      if (QNetworkReply::NoError == reply->error ())
        {
          (this->*handler) (reply->readAll ());
        }
      else
        {
          ++failed_;
        }
      if (0 == pending_)
        {
          updated_ = QDateTime::currentDateTimeUtc ();
          update_tiles ();
          update_bands ();
          QString text = tr ("Space weather and aurora: NOAA Space Weather Prediction Center, updated %1 UTC. "
                             "Sun: NASA SDO. Maps: NASA Blue Marble and Black Marble, Natural Earth; rain and snow: NASA GIBS.")
            .arg (updated_.toString ("hh:mm"));
          if (overlay_status_.size ()) text += ' ' + overlay_status_;
          if (failed_) text += ' ' + tr ("(%n item(s) couldn't be reached; will try again in 15 minutes)", "", failed_);
          status_->setText (text);
        }
    });
}

void ConditionsWindow::on_flux (QByteArray const& data)
{
  auto const recs = records (data);
  if (recs.size ()) sfi_ = field (recs.last (), {"flux"});
}

void ConditionsWindow::on_kindex (QByteArray const& data)
{
  auto const recs = records (data);
  if (recs.isEmpty ()) return;
  kp_ = field (recs.last (), {"Kp", "kp_index"});
  // the A index is the average of the last eight 3-hour values
  double sum = 0;
  int n = 0;
  for (int i = recs.size () - 1; i >= 0 && n < 8; --i)
    {
      double const a = field (recs[i], {"a_running", "ap"});
      if (!qIsNaN (a)) {sum += a; ++n;}
    }
  a_index_ = n ? sum / n : qQNaN ();
}

void ConditionsWindow::on_wind (QByteArray const& data)
{
  auto const recs = records (data);
  if (recs.size ()) wind_ = field (recs.last (), {"proton_speed", "WindSpeed", "speed"});
}

void ConditionsWindow::on_mag (QByteArray const& data)
{
  auto const recs = records (data);
  if (recs.isEmpty ()) return;
  bz_ = field (recs.last (), {"bz_gsm", "Bz"});
  bt_ = field (recs.last (), {"bt", "Bt"});
}


void ConditionsWindow::on_xray (QByteArray const& data)
{
  // a day of one-minute readings in two wavebands
  auto const recs = records (data);
  QVector<QPointF> long_band, short_band;
  for (auto const& r : recs)
    {
      double const flux = field (r, {"flux"});
      if (qIsNaN (flux) || flux <= 0) continue;
      auto const t = QDateTime::fromString (r.value ("time_tag").toString (), Qt::ISODate);
      if (!t.isValid ()) continue;
      QPointF const pt {t.toMSecsSinceEpoch () / 1000., flux};
      auto const energy = r.value ("energy").toString ();
      if ("0.1-0.8nm" == energy) long_band << pt;
      else if ("0.05-0.4nm" == energy) short_band << pt;
    }
  if (long_band.size ())
    {
      xray_ = long_band.last ().y ();
      xray_graph_->long_band = long_band;
      xray_graph_->short_band = short_band;
      xray_graph_->update ();
    }
}

void ConditionsWindow::on_daily (QByteArray const& data)
{
  // last line of the daily table: year month day flux sunspots ...
  auto const lines = QString::fromLatin1 (data).split ('\n');
  for (int i = lines.size () - 1; i >= 0; --i)
    {
      auto const line = lines[i].trimmed ();
      if (line.isEmpty () || !line[0].isDigit ()) continue;
      auto const parts = line.split (QRegularExpression {"\\s+"});
      if (parts.size () > 4)
        {
          bool ok;
          double const ssn = parts[4].toDouble (&ok);
          if (ok && ssn >= 0) ssn_ = ssn;
          if (qIsNaN (sfi_))
            {
              double const f = parts[3].toDouble (&ok);
              if (ok && f > 0) sfi_ = f;
            }
        }
      break;
    }
}

void ConditionsWindow::update_tiles ()
{
  auto set = [this] (QString const& key, QString const& value, QString const& note, int level) {
    auto const& t = tiles_[key];
    t.value->setText (value);
    t.note->setText (note);
    auto const c = level_color (level);
    t.value->setStyleSheet (big_css (1.7) + (level < 0 ? QString {} : QString {" color: %1;"}.arg (c.name ())));
  };
  QString const none {QChar {0x2014}};
  if (qIsNaN (sfi_)) set ("sfi", none, {}, -1);
  else set ("sfi", QString::number (qRound (sfi_)), sfi_ >= 150 ? tr ("high") : sfi_ >= 100 ? tr ("moderate") : tr ("low"),
            sfi_ >= 150 ? 2 : sfi_ >= 90 ? 1 : 0);
  if (qIsNaN (ssn_)) set ("ssn", none, {}, -1);
  else set ("ssn", QString::number (qRound (ssn_)), {}, -1);
  if (qIsNaN (kp_)) set ("k", none, {}, -1);
  else set ("k", QString::number (kp_, 'f', kp_ < 10 && std::fmod (kp_, 1.) > 0.01 ? 1 : 0),
            kp_ >= 5 ? tr ("storm") : kp_ >= 4 ? tr ("active") : kp_ >= 3 ? tr ("unsettled") : tr ("quiet"),
            kp_ >= 5 ? 0 : kp_ >= 3 ? 1 : 2);
  if (qIsNaN (a_index_)) set ("a", none, {}, -1);
  else set ("a", QString::number (qRound (a_index_)),
            a_index_ >= 30 ? tr ("stormy") : a_index_ >= 10 ? tr ("unsettled") : tr ("quiet"),
            a_index_ >= 30 ? 0 : a_index_ >= 10 ? 1 : 2);
  if (qIsNaN (xray_)) set ("xray", none, {}, -1);
  else
    {
      int level;
      auto const cls = xray_class (xray_, &level);
      set ("xray", cls, level < 2 ? tr ("flare") : tr ("normal"), level);
    }
  if (qIsNaN (wind_)) set ("wind", none, {}, -1);
  else set ("wind", QString::number (qRound (wind_)), tr ("km/s"), wind_ >= 600 ? 0 : wind_ >= 500 ? 1 : 2);
  if (qIsNaN (bz_)) set ("bz", none, {}, -1);
  else set ("bz", QString::number (bz_, 'f', 0), bz_ <= -10 ? tr ("south, storms likely") : bz_ < 0 ? tr ("southward") : tr ("northward"),
            bz_ <= -10 ? 0 : bz_ <= -5 ? 1 : 2);
}

void ConditionsWindow::update_bands ()
{
  QStringList const words {tr ("Poor"), tr ("Fair"), tr ("Good")};
  for (int g = 0; g < 4; ++g)
    {
      for (int dn = 0; dn < 2; ++dn)
        {
          int const level = band_estimate (g, 0 == dn, sfi_, kp_);
          auto * cell = band_cells_[g][dn];
          if (level < 0)
            {
              cell->setText (QString {QChar {0x2014}});
              cell->setStyleSheet ({});
              continue;
            }
          cell->setText (words[level]);
          cell->setStyleSheet (QString {"QLabel {background-color: %1; color: #000000; border-radius: 4px; padding: 2px 6px; font-weight: bold;}"}
                               .arg (level_color (level).name ()));
        }
    }
}


void ConditionsWindow::update_de_dx ()
{
  auto const now = QDateTime::currentDateTimeUtc ();
  auto sun_line = [] (double lat, double lon) {
    QDateTime rise, set;
    sun_times (lat, lon, rise, set);
    return tr ("Sunrise %1 &nbsp;Sunset %2 UTC")
      .arg (rise.isValid () ? rise.toString ("hh:mm") : QString {"--:--"})
      .arg (set.isValid () ? set.toString ("hh:mm") : QString {"--:--"});
  };

  // you
  QString de = QString {"<span style=\"font-size:large\"><b>DE</b> &nbsp;%1</span>"}
    .arg (my_call_.size () ? slashed (my_call_).toHtmlEscaped () : QString {});
  double my_lat, my_lon;
  bool const have_me = grid_to_latlon (my_grid_, my_lat, my_lon);
  if (have_me)
    {
      de += "<br/>" + my_grid_.left (6).toUpper () + " &nbsp;" + latlon_text (my_lat, my_lon);
      de += "<br/>" + tr ("Local time %1").arg (now.toLocalTime ().toString ("h:mm AP"));
      de += "<br/>" + sun_line (my_lat, my_lon);
    }
  else
    {
      de += "<br/>" + tr ("Put your grid in Settings > General to see your sunrise and sunset.");
    }
  de_info_->setText (de);

  // the station being worked
  QString dx = QString {"<span style=\"font-size:large\"><b>DX</b> &nbsp;%1</span>"}.arg (slashed (dx_call_).toHtmlEscaped ());
  double dx_lat, dx_lon;
  bool have_dx = dx_call_.size () && grid_to_latlon (dx_grid_, dx_lat, dx_lon);
  bool approx = false;
  if (!have_dx && dx_call_.size () && map_->stations.contains (dx_call_))
    {
      dx_lat = map_->stations[dx_call_].lat;
      dx_lon = map_->stations[dx_call_].lon;
      approx = map_->stations[dx_call_].approx;
      have_dx = true;
    }
  if (dx_call_.isEmpty ())
    {
      dx += "<br/>" + tr ("Nobody yet. Double-click a station in Band Activity or on the map.");
    }
  else if (!have_dx)
    {
      dx += "<br/>" + tr ("No grid square yet.");
    }
  else
    {
      dx += "<br/>" + (approx ? tr ("Country only") : dx_grid_.left (6).toUpper ()) + " &nbsp;" + latlon_text (dx_lat, dx_lon);
      // local time from the longitude; time zones don't always follow it
      int const offset = qRound (dx_lon / 15.);
      dx += "<br/>" + tr ("About %1 local").arg (now.addSecs (offset * 3600).toString ("h:mm AP"));
      dx += "<br/>" + sun_line (dx_lat, dx_lon);
      if (have_me)
        {
          double dist, bearing;
          path_to (my_lat, my_lon, dx_lat, dx_lon, dist, bearing);
          double const km = dist * 6371., long_km = 40030. - km;
          auto distance = [this] (double km) {
            return QString {"%1 %2"}.arg (QLocale {}.toString (qRound (miles_ ? km * 0.621371 : km)))
              .arg (miles_ ? tr ("mi") : tr ("km"));
          };
          dx += "<br/>" + tr ("Short path %1 at %2%3").arg (distance (km)).arg (qRound (bearing)).arg (QChar {0x00B0});
          dx += "<br/>" + tr ("Long path %1 at %2%3").arg (distance (long_km))
            .arg (qRound (std::fmod (bearing + 180., 360.))).arg (QChar {0x00B0});
        }
    }
  dx_info_->setText (dx);
}

void ConditionsWindow::set_my_call (QString const& call)
{
  my_call_ = call.trimmed ().toUpper ();
  call_label_->setText (slashed (my_call_));
  update_de_dx ();
}

void ConditionsWindow::set_my_grid (QString const& grid)
{
  if (grid == my_grid_) return;
  my_grid_ = grid;
  double lat, lon;
  if (grid_to_latlon (grid, lat, lon)) map_->set_my (lat, lon);
  update_de_dx ();
}

void ConditionsWindow::set_miles (bool miles)
{
  miles_ = miles;
  map_->miles = miles;
  update_de_dx ();
}

void ConditionsWindow::set_dx (QString const& call, QString const& grid)
{
  dx_call_ = call;
  dx_grid_ = grid;
  double lat, lon;
  if (call.size () && grid_to_latlon (grid, lat, lon))
    {
      map_->set_dx (call, lat, lon);
    }
  else if (call.size () && map_->stations.contains (call))
    {
      auto const& s = map_->stations[call];
      map_->set_dx (call, s.lat, s.lon);
    }
  else
    {
      map_->set_dx (call, qQNaN (), qQNaN ());
    }
  update_de_dx ();
}

void ConditionsWindow::add_station (QString const& call, QString const& grid, int snr, bool cq, bool to_me,
                                    QColor const& highlight, double country_lat, double country_lon)
{
  if (call.isEmpty ()) return;
  double lat, lon;
  bool approx = false;
  if (!grid_to_latlon (grid, lat, lon))
    {
      // keep a better position heard earlier
      if (map_->stations.contains (call) && !map_->stations[call].approx)
        {
          lat = map_->stations[call].lat;
          lon = map_->stations[call].lon;
        }
      else if (!qIsNaN (country_lat) && !qIsNaN (country_lon))
        {
          lat = country_lat;
          lon = country_lon;
          approx = true;
        }
      else
        {
          return;
        }
    }
  map_->stations[call] = Station {lat, lon, QDateTime::currentDateTimeUtc (), snr, cq, to_me, approx, highlight};
  if (isVisible ()) map_->update ();
}

void ConditionsWindow::clear_stations ()
{
  map_->stations.clear ();
  map_->update ();
}

bool ConditionsWindow::grid_to_latlon (QString const& grid_in, double& lat, double& lon)
{
  auto const g = grid_in.trimmed ().toUpper ();
  if (g.size () < 4) return false;
  if (g[0] < 'A' || g[0] > 'R' || g[1] < 'A' || g[1] > 'R' || !g[2].isDigit () || !g[3].isDigit ()) return false;
  if (g.startsWith ("RR73")) return false;        // a message, not a grid
  lon = (g[0].unicode () - 'A') * 20. - 180. + (g[2].unicode () - '0') * 2.;
  lat = (g[1].unicode () - 'A') * 10. - 90. + (g[3].unicode () - '0') * 1.;
  if (g.size () >= 6 && g[4] >= 'A' && g[4] <= 'X' && g[5] >= 'A' && g[5] <= 'X')
    {
      lon += (g[4].unicode () - 'A') * (2. / 24.) + 1. / 24.;
      lat += (g[5].unicode () - 'A') * (1. / 24.) + 0.5 / 24.;
    }
  else
    {
      lon += 1.;
      lat += 0.5;
    }
  return true;
}
