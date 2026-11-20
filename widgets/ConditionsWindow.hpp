// -*- Mode: C++ -*-
#ifndef CONDITIONS_WINDOW_HPP_
#define CONDITIONS_WINDOW_HPP_

#include <QWidget>
#include <QSet>
#include <QDateTime>
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
  // the log on the band the radio is on: calls and grids worked there
  // all_grids and today_all_grids are the same on every band
  void set_worked (QString const& band, QSet<QString> const& calls, QSet<QString> const& grids,
                   QSet<QString> const& today_calls, QSet<QString> const& today_grids,
                   QSet<QString> const& all_grids, QSet<QString> const& today_all_grids);
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

  // the QRZ details (photo, name, place, QRZ page button), shown at the
  // top of the DX box
  void set_call_info (QWidget *);
  Q_SIGNAL void call_colors_changed ();
  Q_SIGNAL void dx_changed_for_tools ();

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
  void show_station_pictures ();
  void apply_station_picture ();
  void save_call_colors ();
  void apply_call_colors ();
  void add_box_action (QString const& key, QString const& text);
  void update_containers ();
  void fetch_sun ();
  void schedule_voacap ();
  void schedule_area ();
  void select_prop_band (int band);
  void run_area ();
  double effective_ssn () const;
  void predicted_muf ();
  void on_predicted_muf (QString const& path);
  void on_area (QString const& path, QString const& label);
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

  struct Tile {QLabel * value {nullptr}; QLabel * caption {nullptr}; QLabel * note {nullptr}; QString tip;};
  QLabel * spacewx_time_ {nullptr};
  QHash<QString, Tile> tiles_;
  QLabel * band_cells_[4][2] {};
  CallSign * call_label_;
  class StationLogo * station_logo_ {nullptr};
  QString station_picture_;
  bool call_auto_ {true};
  QColor call_first_;
  QColor call_second_;
  bool call_gradient_ {true};
  QLabel * utc_clock_;
  QWidget * clock_column_ {nullptr};
  QString clock_column_key_;
  void fix_clock_width ();
  QLabel * utc_date_;
  QLabel * local_clock_;
  QLabel * de_info_;
  QLabel * dx_info_;
  QWidget * call_info_ {nullptr};
  bool eventFilter (QObject *, QEvent *) override;
  XRayGraph * xray_graph_;
  SunImage * sun_image_;
  QStringList sun_channels_;
  class QToolButton * sun_pick_ {nullptr};
  class QActionGroup * sun_pictures_ {nullptr};
  class QAction * sun_movie_action_ {nullptr};
  QString sun_channel_;                    // the picture showing now
  QTimer * sun_rotate_ {nullptr};
  QTimer * sun_movie_timer_ {nullptr};
  QVector<QPair<QDateTime, QImage>> sun_frames_;
  QString sun_frames_channel_;
  int sun_frame_ {0};
  int sun_frames_wanted_ {0};
  QHash<QString, QPair<QDateTime, QImage>> sun_cache_;
  QString sun_choice () const;             // a channel, or "rotate"
  void show_sun_channel (QString const& channel);
  void load_sun_movie ();
  void stop_sun_movie ();
  void gray_line_tool ();
  class QDialog * gray_line_ {nullptr};
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
  QAbstractButton * prop_box_ {nullptr};
  int prop_band_ {4};
  QTimer * area_timer_ {nullptr};
  class QProcess * area_process_ {nullptr};
  bool area_again_ {false};
  class QProcess * muf_process_ {nullptr};
  bool muf_predicted_ {false};
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
  QAbstractButton * cities_box_;
  QAbstractButton * worked_grids_box_;
  QAbstractButton * psk_box_;
  int worked_mode_ {0};          // 0 worked before, 1 worked today, 2 hidden
  QTimer * psk_timer_ {nullptr};
  QTimer * psk_tick_ {nullptr};
  QDateTime psk_asked_;
  class PskLive * psk_live_ {nullptr};
  class QAction * grids_all_action_ {nullptr};
  class QAction * psk_all_action_ {nullptr};
  class QAction * psk_live_action_ {nullptr};
  class QActionGroup * psk_minutes_group_ {nullptr};
  void fetch_psk ();
  void update_psk_live ();
  void map_colours_dialog ();
  void pick_map_colour (QString const& key, QString const& name);
  void load_map_colours ();
  void apply_map_colours ();
  QHash<QString, QColor> map_colours_;     // the ones chosen in Map colours, by key
  QAbstractButton * aurora_box_;
  QAbstractButton * rain_box_;
  QAbstractButton * news_box_ {nullptr};
  class QToolButton * style_button_ {nullptr};
  class QToolButton * projection_button_ {nullptr};
  class QToolButton * grid_button_ {nullptr};
  class QActionGroup * colour_by_ {nullptr};
  class NewsTicker * news_ {nullptr};
  QTimer * news_timer_ {nullptr};
  void fetch_news ();
  void fit_dx_box ();
  void update_sun_corners (QDateTime const& now);
  void fetch_conditions_map ();          // DRAP or the MUF, whichever colours the map
  void fit_outlook ();
  void update_status ();
  bool compact_outlook_ {false};
  int outlook_words_width_ {0};
  int outlook_dots_width_ {0};
  class QHBoxLayout * row1_ {nullptr};
  class QLayout * row2_ {nullptr};
  QWidget * map_bar_ {nullptr};
  void resizeEvent (QResizeEvent *) override;
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
