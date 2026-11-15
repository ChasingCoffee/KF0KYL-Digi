// -*- Mode: C++ -*-
#ifndef CONDITIONS_WINDOW_HPP_
#define CONDITIONS_WINDOW_HPP_

#include <QWidget>
#include <QHash>
#include <QDateTime>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QColor>
#include <QtNumeric>

class QNetworkAccessManager;
class QNetworkReply;
class QLabel;
class QTimer;
class QGridLayout;
class QSettings;

//
// ConditionsWindow - a separate window with space weather and band
// conditions, a gray-line world map with the stations being decoded,
// and clocks with sunrise and sunset times.
//
// Space weather comes from NOAA's Space Weather Prediction Center
// (public data); the map outline is from Natural Earth (public domain);
// the day/night shading, sun times and band estimates are worked out
// here, so they need no internet.
//
class ConditionsMap;

class ConditionsWindow final
  : public QWidget
{
  Q_OBJECT

public:
  explicit ConditionsWindow (QSettings *, QWidget * parent = nullptr);
  ~ConditionsWindow ();

  void set_my_grid (QString const& grid);
  void set_dx (QString const& call, QString const& grid);
  // a decoded station; grid may be empty, then lat/lon come from the
  // callsign's country (approx = true)
  // highlight is the colour the decode is shown in in Band Activity
  void add_station (QString const& call, QString const& grid, int snr, bool cq, bool to_me,
                    QColor const& highlight = QColor {},
                    double country_lat = qQNaN (), double country_lon = qQNaN ());
  void mark_called (QString const& call);          // a station you transmitted to
  void clear_stations ();
  void scheme_changed ();
  // the main window's highlight colours (Settings > Colors), so the map
  // uses the same colours as Band Activity
  void set_highlight_colors (QColor const& cq, QColor const& my_call, QColor const& tx);

  // Maidenhead locator to latitude/longitude (centre of the square)
  static bool grid_to_latlon (QString const& grid, double& lat, double& lon);

protected:
  void showEvent (QShowEvent *) override;
  void hideEvent (QHideEvent *) override;
  void closeEvent (QCloseEvent *) override;

private:
  void build_ui ();
  QWidget * build_map_bar ();
  void save_map_options ();
  void refresh_overlays ();            // aurora and rain, when switched on
  void update_legend ();
  void refresh ();                     // fetch NOAA data
  void tick ();                        // once a second: clocks; once a minute: map
  void request (QString const& url, void (ConditionsWindow::*handler) (QByteArray const&));
  void on_flux (QByteArray const&);
  void on_kindex (QByteArray const&);
  void on_wind (QByteArray const&);
  void on_mag (QByteArray const&);
  void on_xray (QByteArray const&);
  void on_daily (QByteArray const&);
  void update_tiles ();
  void update_bands ();
  void update_sun_times ();
  void apply_colors ();

  QSettings * settings_;
  QNetworkAccessManager * network_;
  QTimer * refresh_timer_;
  QTimer * clock_timer_;
  ConditionsMap * map_;

  struct Tile {QLabel * value; QLabel * caption; QLabel * note;};
  QHash<QString, Tile> tiles_;
  QLabel * band_cells_[4][2] {};
  QLabel * utc_clock_;
  QLabel * local_clock_;
  QLabel * my_sun_;
  QLabel * dx_sun_;
  QLabel * status_;
  QTimer * overlay_timer_ {nullptr};
  class QComboBox * style_box_;
  class QCheckBox * gray_box_;
  class QCheckBox * grid_box_;
  class QCheckBox * aurora_box_;
  class QCheckBox * rain_box_;
  class QCheckBox * show_cq_;
  class QCheckBox * show_me_;
  class QCheckBox * show_called_;
  class QCheckBox * show_others_;
  QColor cq_color_ {"#3FB950"};
  QColor my_call_color_ {"#FF4D4D"};
  QColor tx_color_ {"#FFD54A"};
  QString overlay_status_;

  // latest values, NaN when unknown
  double sfi_;
  double ssn_;
  double kp_;
  double a_index_;
  double wind_;
  double bz_;
  double bt_;
  double xray_;
  QDateTime updated_;
  int pending_ {0};
  int failed_ {0};

  QString my_grid_;
  QString dx_call_;
  QString dx_grid_;
};

#endif
