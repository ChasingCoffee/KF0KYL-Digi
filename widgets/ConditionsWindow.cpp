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
#include <QComboBox>
#include <QCheckBox>
#include <QIcon>
#include <QPixmap>
#include <QSet>
#include <QUrlQuery>

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
}

//
// The map. Three looks: satellite pictures of the Earth by day with the
// lights of the cities at night (NASA Blue Marble and Black Marble,
// public domain), the same by day only, or plain land and borders
// (Natural Earth, public domain) tinted to suit the colour scheme.
// Optional overlays: gray line, Maidenhead grid, NOAA aurora forecast and
// NASA rain and snow. Stations heard in the last 30 minutes are drawn in
// the colours the main window highlights them with.
//
class ConditionsMap final
  : public QWidget
{
public:
  enum Style {DayNight = 0, Satellite = 1, Countries = 2};

  explicit ConditionsMap (QWidget * parent)
    : QWidget {parent}
    , land_ {QImage {":/map_land.png"}.convertToFormat (QImage::Format_Grayscale8)}
    , borders_ {QImage {":/map_borders.png"}.convertToFormat (QImage::Format_Grayscale8)}
  {
    setMinimumSize (360, 180);
    setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking (true);
    retint ();
    update_night ();
  }

  // options
  Style style {DayNight};
  bool gray_line {true};
  bool grid {false};
  bool show_aurora {false};
  bool show_rain {false};
  bool show_cq {true};
  bool show_me {true};
  bool show_called {true};
  bool show_others {true};
  QColor cq_color {"#3FB950"};
  QColor my_call_color {"#FF4D4D"};
  QColor tx_color {"#FFD54A"};
  QHash<QString, Station> stations;
  QSet<QString> called;              // stations you transmitted to

  void set_style (Style s)
  {
    style = s;
    if (style != Countries && day_.isNull ())
      {
        // load the satellite pictures the first time they're wanted
        day_ = QImage {":/map_day.jpg"}.convertToFormat (QImage::Format_RGB32);
        night_lights_ = QImage {":/map_night.jpg"}.convertToFormat (QImage::Format_RGB32);
        day_small_ = day_.scaled (1024, 512, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        night_small_ = night_lights_.scaled (1024, 512, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
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
    night_scaled_ = QImage {};
    rebuild_base ();
  }

  void set_my (double lat, double lon) {my_lat_ = lat; my_lon_ = lon; update ();}
  void set_dx (QString const& call, double lat, double lon) {dx_call_ = call; dx_lat_ = lat; dx_lon_ = lon; update ();}

  void set_aurora (QImage const& img) {aurora_ = img; aurora_scaled_ = QImage {}; update ();}
  void set_rain (QImage const& img) {rain_ = img; rain_scaled_ = QImage {}; update ();}

protected:
  void resizeEvent (QResizeEvent *) override
  {
    scaled_ = night_scaled_ = aurora_scaled_ = rain_scaled_ = QImage {};
  }

  void paintEvent (QPaintEvent *) override
  {
    QPainter p {this};
    p.setRenderHint (QPainter::Antialiasing);
    p.fillRect (rect (), palette ().window ());
    auto const r = map_rect ();
    auto fit = [&r] (QImage const& src, QImage& cache) -> QImage const& {
      if (cache.size () != r.size () && !src.isNull ())
        {
          cache = src.scaled (r.size (), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
      return cache;
    };
    p.drawImage (r.topLeft (), fit (base_, scaled_));
    // the day/night picture already shows the night side
    if (gray_line && style != DayNight) p.drawImage (r.topLeft (), fit (night_, night_scaled_));
    if (show_rain && !rain_.isNull ()) p.drawImage (r.topLeft (), fit (rain_, rain_scaled_));
    if (show_aurora && !aurora_.isNull ()) p.drawImage (r.topLeft (), fit (aurora_, aurora_scaled_));
    if (grid) draw_grid (p, r);

    // the sun
    auto const sun = to_point (sun_lat_, sun_lon_);
    p.setPen (QPen {QColor {"#FFB000"}, 2});
    p.setBrush (QColor {"#FFD54A"});
    p.drawEllipse (sun, 7, 7);

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
        p.setFont (f);
        // outlined label so it reads on any background
        QPainterPath text;
        text.addText (dx + QPointF {10, 5}, f, dx_call_);
        p.setPen (QPen {QColor {0, 0, 0, 200}, 3});
        p.setBrush (Qt::NoBrush);
        p.drawPath (text);
        p.setPen (Qt::NoPen);
        p.setBrush (Qt::white);
        p.drawPath (text);
      }
    p.setPen (QPen {palette ().mid ().color (), 1});
    p.setBrush (Qt::NoBrush);
    p.drawRect (QRectF {r}.adjusted (0.5, 0.5, -0.5, -0.5));
  }

public:
  std::function<void (QString const&, Qt::KeyboardModifiers)> on_double_click;

protected:

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
    QWidget::mouseMoveEvent (e);
  }

  bool event (QEvent * e) override
  {
    if (e->type () == QEvent::ToolTip)
      {
        auto * help = static_cast<QHelpEvent *> (e);
        auto const call = station_at (help->pos ());
        if (call.size ())
          {
            auto const& s = stations[call];
            QString what;
            if (s.to_me) what = QObject::tr ("calling you");
            else if (called.contains (call)) what = QObject::tr ("you called");
            else if (s.cq) what = QObject::tr ("calling CQ");
            QToolTip::showText (help->globalPos (),
                                QString {"<b>%1</b> &nbsp;%2 dB &nbsp;%3 UTC &nbsp;%4<br/>%5"}
                                .arg (call).arg (s.snr).arg (s.heard.toString ("hh:mm")).arg (what)
                                .arg (QObject::tr ("Double-click to call")), this);
          }
        else
          {
            QToolTip::hideText ();
          }
        return true;
      }
    return QWidget::event (e);
  }

private:
  void rebuild_base ()
  {
    switch (style)
      {
      case Countries:
        base_ = countries_;
        break;

      case Satellite:
        if (day_.isNull ()) {base_ = countries_; break;}
        base_ = day_.convertToFormat (QImage::Format_ARGB32_Premultiplied);
        {
          QPainter p {&base_};
          p.setOpacity (0.45);
          p.drawImage (0, 0, border_lines_);
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
    scaled_ = QImage {};
    update ();
  }

  void draw_grid (QPainter& p, QRect const& r)
  {
    // Maidenhead fields: 20 by 10 degrees, AA to RR
    p.setPen (QPen {QColor {255, 255, 255, 70}, 1});
    for (int i = 1; i < 18; ++i)
      {
        double const x = r.left () + r.width () * i / 18.;
        p.drawLine (QPointF {x, double (r.top ())}, QPointF {x, double (r.bottom ())});
        double const y = r.top () + r.height () * i / 18.;
        p.drawLine (QPointF {double (r.left ()), y}, QPointF {double (r.right ()), y});
      }
    if (r.width () < 600) return;
    auto f = font ();
    f.setPointSizeF (qMax (6., f.pointSizeF () * 0.8));
    p.setFont (f);
    p.setPen (QColor {255, 255, 255, 140});
    for (int i = 0; i < 18; ++i)
      {
        for (int j = 0; j < 18; ++j)
          {
            QString field;
            field += QChar ('A' + i);
            field += QChar ('A' + (17 - j));
            p.drawText (QPointF {r.left () + r.width () * i / 18. + 3, r.top () + r.height () * j / 18. + f.pointSizeF () + 3}, field);
          }
      }
  }

  QRect map_rect () const
  {
    // keep the map 2:1 and centred
    int w = width (), h = height ();
    if (w > 2 * h) w = 2 * h; else h = w / 2;
    return QRect {(width () - w) / 2, (height () - h) / 2, w, h};
  }

  QPointF to_point (double lat, double lon) const
  {
    auto const r = map_rect ();
    return {r.left () + (lon + 180.) / 360. * r.width (), r.top () + (90. - lat) / 180. * r.height ()};
  }

  void draw_great_circle (QPainter& p, double lat1, double lon1, double lat2, double lon2)
  {
    double const p1 = rad (lat1), l1 = rad (lon1), p2 = rad (lat2), l2 = rad (lon2);
    double const d = 2 * std::asin (std::sqrt (std::pow (std::sin ((p2 - p1) / 2), 2)
                                               + std::cos (p1) * std::cos (p2) * std::pow (std::sin ((l2 - l1) / 2), 2)));
    if (d < 1e-6) return;
    QPainterPath path;
    QPointF last;
    for (int i = 0; i <= 100; ++i)
      {
        double const f = i / 100.;
        double const A = std::sin ((1 - f) * d) / std::sin (d), B = std::sin (f * d) / std::sin (d);
        double const x = A * std::cos (p1) * std::cos (l1) + B * std::cos (p2) * std::cos (l2);
        double const y = A * std::cos (p1) * std::sin (l1) + B * std::cos (p2) * std::sin (l2);
        double const z = A * std::sin (p1) + B * std::sin (p2);
        auto const pt = to_point (deg (std::atan2 (z, std::sqrt (x * x + y * y))), deg (std::atan2 (y, x)));
        // start a new piece where the path crosses the edge of the map
        if (0 == i || std::abs (pt.x () - last.x ()) > map_rect ().width () / 2.) path.moveTo (pt);
        else path.lineTo (pt);
        last = pt;
      }
    p.setBrush (Qt::NoBrush);
    p.drawPath (path);
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
  QImage base_;
  QImage scaled_;
  QImage night_;
  QImage night_scaled_;
  QVector<double> shade_;
  QImage aurora_;
  QImage aurora_scaled_;
  QImage rain_;
  QImage rain_scaled_;
  double sun_lat_ {0.};
  double sun_lon_ {0.};
  double my_lat_ {qQNaN ()};
  double my_lon_ {qQNaN ()};
  double dx_lat_ {qQNaN ()};
  double dx_lon_ {qQNaN ()};
  QString dx_call_;
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

void ConditionsWindow::build_ui ()
{
  auto * outer = new QVBoxLayout {this};
  outer->setContentsMargins (10, 10, 10, 8);
  outer->setSpacing (8);

  // ---- space weather tiles ----
  auto * tiles = new QGridLayout;
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
      auto * frame = new QFrame {this};
      frame->setObjectName ("conditionsTile");
      frame->setFrameShape (QFrame::StyledPanel);
      frame->setToolTip (tr (d.tip));
      auto * v = new QVBoxLayout {frame};
      v->setContentsMargins (8, 6, 8, 6);
      v->setSpacing (0);
      Tile t;
      t.caption = new QLabel {tr (d.caption), frame};
      t.caption->setAlignment (Qt::AlignCenter);
      t.value = new QLabel {QString {QChar {0x2014}}, frame};
      t.value->setAlignment (Qt::AlignCenter);
      auto f = t.value->font ();
      f.setPointSizeF (f.pointSizeF () * 1.9);
      f.setBold (true);
      t.value->setFont (f);
      t.note = new QLabel {frame};
      t.note->setAlignment (Qt::AlignCenter);
      v->addWidget (t.caption);
      v->addWidget (t.value);
      v->addWidget (t.note);
      tiles_[d.key] = t;
      tiles->addWidget (frame, 0, col++);
    }
  outer->addLayout (tiles);

  // ---- map, with band outlook and clocks beside it ----
  auto * middle = new QHBoxLayout;
  middle->setSpacing (10);
  map_ = new ConditionsMap {this};
  map_->on_double_click = [this] (QString const& call, Qt::KeyboardModifiers modifiers) {
    Q_EMIT station_double_clicked (call, modifiers);
  };
  auto * map_column = new QVBoxLayout;
  map_column->setSpacing (4);
  map_column->addWidget (build_map_bar ());
  map_column->addWidget (map_, 1);
  middle->addLayout (map_column, 1);

  auto * side = new QVBoxLayout;
  side->setSpacing (6);
  auto * bands_title = new QLabel {tr ("<b>Band outlook</b>"), this};
  bands_title->setToolTip (tr ("A rough estimate from the solar flux and K index, not a forecast for any one path."));
  side->addWidget (bands_title);
  auto * bands = new QGridLayout;
  bands->setHorizontalSpacing (6);
  bands->setVerticalSpacing (4);
  bands->addWidget (new QLabel {tr ("Day"), this}, 0, 1, Qt::AlignCenter);
  bands->addWidget (new QLabel {tr ("Night"), this}, 0, 2, Qt::AlignCenter);
  char const * groups[] = {"80-40m", "30-20m", "17-15m", "12-10m"};
  for (int g = 0; g < 4; ++g)
    {
      bands->addWidget (new QLabel {groups[g], this}, g + 1, 0);
      for (int dn = 0; dn < 2; ++dn)
        {
          auto * cell = new QLabel {QString {QChar {0x2014}}, this};
          cell->setAlignment (Qt::AlignCenter);
          cell->setMinimumWidth (56);
          band_cells_[g][dn] = cell;
          bands->addWidget (cell, g + 1, dn + 1);
        }
    }
  side->addLayout (bands);
  auto * est = new QLabel {tr ("Estimated from solar flux and K"), this};
  est->setEnabled (false);
  side->addWidget (est);
  side->addSpacing (10);

  utc_clock_ = new QLabel {this};
  local_clock_ = new QLabel {this};
  auto f = utc_clock_->font ();
  f.setPointSizeF (f.pointSizeF () * 1.6);
  f.setBold (true);
  utc_clock_->setFont (f);
  local_clock_->setFont (f);
  side->addWidget (new QLabel {tr ("<b>UTC</b>"), this});
  side->addWidget (utc_clock_);
  side->addWidget (new QLabel {tr ("<b>Local</b>"), this});
  side->addWidget (local_clock_);
  side->addSpacing (10);
  my_sun_ = new QLabel {this};
  my_sun_->setWordWrap (true);
  dx_sun_ = new QLabel {this};
  dx_sun_->setWordWrap (true);
  side->addWidget (my_sun_);
  side->addWidget (dx_sun_);
  side->addStretch ();

  middle->addLayout (side);
  outer->addLayout (middle, 1);

  status_ = new QLabel {this};
  status_->setEnabled (false);
  status_->setWordWrap (true);
  outer->addWidget (status_);

  resize (1100, 640);
  apply_colors ();
}

QWidget * ConditionsWindow::build_map_bar ()
{
  auto * bar = new QWidget {this};
  auto * v = new QVBoxLayout {bar};
  v->setContentsMargins (0, 0, 0, 0);
  v->setSpacing (2);

  // first row: which map, and what to draw over it
  auto * row1 = new QHBoxLayout;
  row1->setSpacing (10);
  row1->addWidget (new QLabel {tr ("Map:"), bar});
  style_box_ = new QComboBox {bar};
  style_box_->addItem (tr ("Satellite, day and night"));
  style_box_->addItem (tr ("Satellite"));
  style_box_->addItem (tr ("Countries"));
  style_box_->setToolTip (tr ("<p><b>Satellite, day and night:</b> the Earth by day, city lights on the night side.</p>"
                              "<p><b>Satellite:</b> the Earth by day everywhere.</p>"
                              "<p><b>Countries:</b> plain land and borders in this colour scheme.</p>"));
  row1->addWidget (style_box_);
  gray_box_ = new QCheckBox {tr ("Gray line"), bar};
  gray_box_->setToolTip (tr ("Shade the night side of the Earth"));
  grid_box_ = new QCheckBox {tr ("Grid squares"), bar};
  grid_box_->setToolTip (tr ("Maidenhead fields (AA to RR)"));
  aurora_box_ = new QCheckBox {tr ("Aurora"), bar};
  aurora_box_->setToolTip (tr ("NOAA's 30-minute aurora forecast. Strong aurora can absorb or reflect HF and VHF signals near the poles."));
  rain_box_ = new QCheckBox {tr ("Rain and snow"), bar};
  rain_box_->setToolTip (tr ("Where it's raining or snowing over the last half hour or so, from NASA satellites"));
  row1->addWidget (gray_box_);
  row1->addWidget (grid_box_);
  row1->addWidget (aurora_box_);
  row1->addWidget (rain_box_);
  row1->addStretch ();
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
  style_box_->setCurrentIndex (qBound (0, settings_->value ("MapStyle", 0).toInt (), 2));
  gray_box_->setChecked (settings_->value ("GrayLine", true).toBool ());
  grid_box_->setChecked (settings_->value ("GridSquares", false).toBool ());
  aurora_box_->setChecked (settings_->value ("Aurora", false).toBool ());
  rain_box_->setChecked (settings_->value ("Rain", false).toBool ());
  show_cq_->setChecked (settings_->value ("ShowCQ", true).toBool ());
  show_me_->setChecked (settings_->value ("ShowCallingMe", true).toBool ());
  show_called_->setChecked (settings_->value ("ShowCalled", true).toBool ());
  show_others_->setChecked (settings_->value ("ShowOthers", true).toBool ());
  settings_->endGroup ();

  auto apply = [this] {
    map_->set_style (static_cast<ConditionsMap::Style> (style_box_->currentIndex ()));
    gray_box_->setEnabled (style_box_->currentIndex () != ConditionsMap::DayNight);
    map_->gray_line = gray_box_->isChecked ();
    map_->grid = grid_box_->isChecked ();
    map_->show_aurora = aurora_box_->isChecked ();
    map_->show_rain = rain_box_->isChecked ();
    map_->show_cq = show_cq_->isChecked ();
    map_->show_me = show_me_->isChecked ();
    map_->show_called = show_called_->isChecked ();
    map_->show_others = show_others_->isChecked ();
    map_->update ();
    save_map_options ();
  };
  connect (style_box_, static_cast<void (QComboBox::*) (int)> (&QComboBox::currentIndexChanged), this, apply);
  for (auto * box : {gray_box_, grid_box_, show_cq_, show_me_, show_called_, show_others_})
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
  settings_->setValue ("GrayLine", gray_box_->isChecked ());
  settings_->setValue ("GridSquares", grid_box_->isChecked ());
  settings_->setValue ("Aurora", aurora_box_->isChecked ());
  settings_->setValue ("Rain", rain_box_->isChecked ());
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
  setStyleSheet (QString {"QFrame#conditionsTile {background-color: %1; border: 1px solid %2; border-radius: 8px;}"}
                 .arg (pal.alternateBase ().color ().name ()).arg (pal.mid ().color ().name ()));
  map_->retint ();
  update_legend ();
  update_tiles ();
  update_bands ();
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
  // fetch now unless the numbers are recent
  if (!updated_.isValid () || updated_.secsTo (QDateTime::currentDateTimeUtc ()) > 10 * 60) refresh ();
}

void ConditionsWindow::hideEvent (QHideEvent * e)
{
  // nothing to fetch or draw while it's closed
  clock_timer_->stop ();
  refresh_timer_->stop ();
  overlay_timer_->stop ();
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
  utc_clock_->setText (now.toString ("hh:mm:ss"));
  local_clock_->setText (now.toLocalTime ().toString ("h:mm:ss AP"));
  if (0 == now.time ().second ())
    {
      map_->update_night ();
      // forget stations not heard for half an hour
      for (auto it = map_->stations.begin (); it != map_->stations.end ();)
        {
          if (it->heard.secsTo (now) > 1800) it = map_->stations.erase (it);
          else ++it;
        }
      if (0 == now.time ().minute () % 10) update_sun_times ();
    }
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
  request (swpc + "json/goes/primary/xrays-6-hour.json", &ConditionsWindow::on_xray);
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
                             "Maps: NASA Blue Marble and Black Marble, Natural Earth; rain and snow: NASA GIBS.")
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
  auto const recs = records (data);
  for (int i = recs.size () - 1; i >= 0; --i)
    {
      if (recs[i].value ("energy").toString () == "0.1-0.8nm")
        {
          xray_ = field (recs[i], {"flux"});
          break;
        }
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
    t.value->setStyleSheet (level < 0 ? QString {} : QString {"color: %1;"}.arg (c.name ()));
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
      char cls = 'A';
      double base = 1e-8;
      if (xray_ >= 1e-4) {cls = 'X'; base = 1e-4;}
      else if (xray_ >= 1e-5) {cls = 'M'; base = 1e-5;}
      else if (xray_ >= 1e-6) {cls = 'C'; base = 1e-6;}
      else if (xray_ >= 1e-7) {cls = 'B'; base = 1e-7;}
      set ("xray", QString {"%1%2"}.arg (cls).arg (xray_ / base, 0, 'f', 1),
           cls == 'X' || cls == 'M' ? tr ("flare") : tr ("normal"),
           cls == 'X' ? 0 : cls == 'M' ? 1 : 2);
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

void ConditionsWindow::update_sun_times ()
{
  auto describe = [] (QString const& who, QString const& grid) {
    double lat, lon;
    if (!grid_to_latlon (grid, lat, lon)) return QString {};
    QDateTime rise, set;
    sun_times (lat, lon, rise, set);
    QString text = QString {"<b>%1</b> (%2)<br/>"}.arg (who.toHtmlEscaped ()).arg (grid.left (6).toUpper ());
    if (rise.isValid ()) text += tr ("Sunrise %1").arg (rise.toString ("hh:mm"));
    else text += tr ("No sunrise today");
    text += " &nbsp; ";
    if (set.isValid ()) text += tr ("Sunset %1").arg (set.toString ("hh:mm"));
    else text += tr ("No sunset today");
    return text + " UTC";
  };
  my_sun_->setText (describe (tr ("You"), my_grid_));
  dx_sun_->setText (dx_call_.size () ? describe (dx_call_, dx_grid_) : QString {});
}

void ConditionsWindow::set_my_grid (QString const& grid)
{
  my_grid_ = grid;
  double lat, lon;
  if (grid_to_latlon (grid, lat, lon)) map_->set_my (lat, lon);
  update_sun_times ();
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
  update_sun_times ();
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
