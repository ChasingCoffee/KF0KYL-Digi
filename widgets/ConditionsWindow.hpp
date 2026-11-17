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
#include <QStringList>
#include <QtNumeric>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QLabel;
class QTimer;
class QGridLayout;
class QSettings;
class QFrame;
class QMenu;
class QVBoxLayout;
class XRayGraph;
class SunImage;
class CallSign;
class QButtonGroup;
class QProgressBar;
class QAbstractButton;
class QLabel;

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

  void set_my_call (QString const& call);
  void set_my_grid (QString const& grid);
  void set_dx (QString const& call, QString const& grid);
  void set_miles (bool miles);         // Settings > General > Display distance in miles
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

  Q_SIGNAL void call_colors_changed ();

  // double-clicking a dot on the map
  Q_SIGNAL void station_double_clicked (QString const& call, Qt::KeyboardModifiers);

  // Maidenhead locator to latitude/longitude (centre of the square)
  static bool grid_to_latlon (QString const& grid, double& lat, double& lon);

protected:
  void showEvent (QShowEvent *) override;
  void hideEvent (QHideEvent *) override;
  void closeEvent (QCloseEvent *) override;

private:
  void build_ui ();
  QWidget * build_map_bar ();
  // a box that can be switched on and off from the Boxes menu
  QFrame * make_box (QString const& key, QString const& title, QVBoxLayout *& body);
  QLabel * box_title (QString const& title, QWidget * parent);
  QAbstractButton * make_chip (QString const& text, QString const& tip, QWidget * parent);
  QWidget * make_segments (QStringList const& labels, QStringList const& tips, QButtonGroup *& group, QWidget * parent);
  void show_call_colors ();
  void save_call_colors ();
  void apply_call_colors ();
  void add_box_action (QString const& key, QString const& text);
  void update_containers ();
  void fetch_sun ();
  void schedule_voacap ();
  void run_voacap ();
  void on_voacap (QString const& path);
  static QString voacap_root ();
  static QString voacap_program ();
  void update_beacons ();
  bool box_on (QString const& key) const;
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
  void update_de_dx ();
  void apply_colors ();

  QSettings * settings_;
  QNetworkAccessManager * network_;
  QTimer * refresh_timer_;
  QTimer * clock_timer_;
  ConditionsMap * map_;

  struct Tile {QLabel * value; QLabel * caption; QLabel * note;};
  QHash<QString, Tile> tiles_;
  QLabel * band_cells_[4][2] {};
  CallSign * call_label_;
  bool call_auto_ {true};
  QColor call_first_;
  QColor call_second_;
  bool call_gradient_ {true};
  QLabel * utc_clock_;
  QLabel * utc_date_;
  QLabel * local_clock_;
  QLabel * de_info_;
  QLabel * dx_info_;
  XRayGraph * xray_graph_;
  SunImage * sun_image_;
  QButtonGroup * sun_group_;
  QStringList sun_channels_;
  QProgressBar * beacon_bar_;
  QTimer * sun_timer_;
  QLabel * beacon_rows_[5] {};
  QLabel * beacon_next_;
  int beacon_slot_ {-1};
  QMenu * boxes_menu_;
  QHash<QString, QWidget *> boxes_;
  QHash<QString, class QAction *> box_actions_;
  QWidget * top_row_;
  class QVBoxLayout * side_ {nullptr};
  QWidget * side_filler_ {nullptr};
  class VoacapGrid * voacap_grid_ {nullptr};
  QLabel * voacap_call_ {nullptr};
  QLabel * voacap_now_ {nullptr};
  class QComboBox * voacap_power_ {nullptr};
  class QComboBox * voacap_mode_ {nullptr};
  class QComboBox * voacap_toa_ {nullptr};
  QButtonGroup * voacap_path_ {nullptr};
  QTimer * voacap_timer_ {nullptr};
  class QProcess * voacap_process_ {nullptr};
  bool voacap_again_ {false};
  QDateTime voacap_ran_;
  QWidget * left_column_;
  class QScrollArea * left_scroll_ {nullptr};
  QLabel * status_;
  QTimer * overlay_timer_ {nullptr};
  QButtonGroup * style_group_;
  QAbstractButton * gray_box_;
  QButtonGroup * projection_group_;
  class QComboBox * grid_box_;
  QAbstractButton * beacons_box_;
  QAbstractButton * aurora_box_;
  QAbstractButton * rain_box_;
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

  bool miles_ {false};
  QString my_call_;
  QString my_grid_;
  QString dx_call_;
  QString dx_grid_;
};

#endif
