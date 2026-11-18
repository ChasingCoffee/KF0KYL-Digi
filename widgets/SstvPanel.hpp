#ifndef SSTV_PANEL_HPP__
#define SSTV_PANEL_HPP__

// The SSTV screen: it covers the main window while SSTV is the mode.
// Receive on the left, the gallery, stations heard and transmit on the
// right.

#include <QWidget>
#include <QDateTime>
#include <QImage>
#include <QVector>
#include <functional>

#include "SstvCodec.hpp"

class QSettings;
class QLabel;
class QComboBox;
class QLineEdit;
class QPushButton;
class QToolButton;
class QProgressBar;
class QTableWidget;
class QHBoxLayout;
class PictureView;

class SstvPanel : public QWidget
{
  Q_OBJECT

public:
  struct Place {QString country; double km {-1.};};

  SstvPanel (QSettings *, QString const& folder, QWidget * parent = nullptr);
  ~SstvPanel ();

  void set_my_call (QString const&);
  void set_my_grid (QString const&);
  void set_frequency (qint64 hz);
  void set_modes (QStringList const& modes);
  void set_place_lookup (std::function<Place (QString const&)> f) {lookup_ = f;}
  void set_transmitting (bool on);
  void restyle ();

  Q_SLOT void add_samples (QVector<short> const&);

  // the SSTV calling frequency for a band, and whether it's on LSB
  static bool calling_frequency (QString const& band, qint64& hz, bool& lsb);

  Q_SIGNAL void mode_chosen (QString const& mode) const;
  Q_SIGNAL void band_chosen (qint64 dial_hz, bool lsb) const;
  Q_SIGNAL void send_audio (QVector<float> const&) const;
  Q_SIGNAL void halt () const;
  Q_SIGNAL void tune (bool) const;
  Q_SIGNAL void log_qso (QString const& call, QString const& rpt_sent, QString const& rpt_rcvd,
                         QDateTime const& on, QDateTime const& off) const;
  Q_SIGNAL void station_heard (QString const& call) const;

private:
  void build ();
  void picture_started (QString const& mode);
  void picture_progress (int rows, int total);
  void picture_finished (QImage const&, QString const& mode, bool complete);
  void call_heard (QString const& call);
  void save_current (QString const& call);
  void load_gallery ();
  void show_gallery_picture (QString const& path);
  void add_heard (QString const& call, QString const& mode, QDateTime const& when);
  void choose_picture ();
  void update_preview ();
  QImage compose () const;
  void send (bool cq);
  void update_send_text ();
  void save_settings () const;

  QSettings * settings_;
  QString folder_;
  sstv::Decoder decoder_;
  std::function<Place (QString const&)> lookup_;
  QString my_call_;
  QString my_grid_;
  bool transmitting_ {false};

  // receive
  PictureView * view_;
  QLabel * mode_label_;
  QToolButton * auto_start_;
  QToolButton * slant_;
  QToolButton * save_all_;
  QLabel * from_;
  QLabel * signal_;
  QLabel * status_;
  QProgressBar * progress_;
  QString last_call_;
  QString last_mode_;
  QString last_saved_;
  QDateTime picture_on_;
  QDateTime picture_off_;
  QImage last_picture_;

  // strip
  QComboBox * mode_combo_;
  QLabel * freq_;
  QComboBox * band_combo_;
  QPushButton * tune_;

  // gallery and stations heard
  QHBoxLayout * gallery_row_;
  QLabel * gallery_count_;
  QTableWidget * heard_;

  // transmit
  QLabel * preview_;
  QComboBox * template_;
  QComboBox * tx_mode_;
  QLineEdit * to_;
  QLineEdit * report_;
  QLineEdit * text_;
  QPushButton * picture_button_;
  QPushButton * send_;
  QPushButton * send_cq_;
  QString picture_path_;
};

#endif
