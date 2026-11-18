#include "SstvPanel.hpp"

#include <QSettings>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QProgressBar>
#include <QTableWidget>
#include <QHeaderView>
#include <QBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QApplication>
#include <QtMath>
#include <algorithm>
#include <cmath>

#include "Win11Theme.hpp"

namespace
{
  QString tok (char const * name) {return Win11Theme::token (name);}

  struct Calling {char const * band; qint64 hz; bool lsb;};
  Calling const calling[] = {
    {"160m", 1890000, true}, {"80m", 3845000, true}, {"40m", 7171000, true},
    {"20m", 14230000, false}, {"17m", 18160000, false}, {"15m", 21340000, false},
    {"12m", 24975000, false}, {"10m", 28680000, false}, {"6m", 50680000, false},
    {"2m", 145500000, false},
  };

  QFrame * card (QWidget * parent)
  {
    auto * f = new QFrame {parent};
    f->setObjectName ("card");
    return f;
  }

  QLabel * caption (QString const& text, QWidget * parent)
  {
    auto * l = new QLabel {text.toUpper (), parent};
    l->setObjectName ("cap");
    return l;
  }

  QToolButton * chip (QString const& text, QString const& tip, QWidget * parent)
  {
    auto * b = new QToolButton {parent};
    b->setObjectName ("chip");
    b->setText (text);
    b->setToolTip (tip);
    b->setCheckable (true);
    b->setCursor (Qt::PointingHandCursor);
    return b;
  }

  QLabel * field (QWidget * parent)
  {
    auto * l = new QLabel {parent};
    l->setObjectName ("field");
    l->setTextFormat (Qt::RichText);
    return l;
  }

  QString small (QString const& s) {return QString {"<span style='color:%1'>%2</span>&nbsp; "}.arg (tok ("text_secondary"), s);}

  // text with a dark outline, so it reads on any picture
  void outlined (QPainter& p, QRectF const& r, int flags, QString const& text, QFont const& font, QColor const& fill)
  {
    QPainterPath path;
    QFontMetricsF const fm {font};
    double x = r.left ();
    if (flags & Qt::AlignHCenter) x = r.center ().x () - fm.horizontalAdvance (text) / 2.;
    double y = r.top () + fm.ascent ();
    if (flags & Qt::AlignBottom) y = r.bottom () - fm.descent ();
    path.addText (QPointF {x, y}, font, text);
    p.setRenderHint (QPainter::Antialiasing);
    p.strokePath (path, QPen {QColor {0, 0, 0, 230}, qMax (2., font.pixelSize () / 7.), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
    p.fillPath (path, fill);
  }
}

// the picture coming in, fitted to the space, with a line showing where
// it has got to
class PictureView : public QWidget
{
public:
  explicit PictureView (QWidget * parent) : QWidget {parent}
  {
    setMinimumSize (320, 240);
    setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
  }

  void set_picture (QImage const& image, int rows, bool live)
  {
    image_ = image;
    rows_ = rows;
    live_ = live;
    update ();
  }

  void set_message (QString const& text) {message_ = text; update ();}

protected:
  void paintEvent (QPaintEvent *) override
  {
    QPainter p {this};
    p.setRenderHint (QPainter::Antialiasing);
    p.setRenderHint (QPainter::SmoothPixmapTransform);
    QRectF const area = QRectF {rect ()}.adjusted (1, 1, -1, -1);
    QPainterPath frame;
    QRectF target = area;
    if (!image_.isNull ())
      {
        QSizeF const size = QSizeF {image_.size ()}.scaled (area.size (), Qt::KeepAspectRatio);
        target = QRectF {QPointF {area.center ().x () - size.width () / 2., area.top ()}, size};
      }
    frame.addRoundedRect (target, 12, 12);
    p.fillPath (frame, QColor {tok ("input")});
    if (!image_.isNull ())
      {
        p.save ();
        p.setClipPath (frame);
        p.drawImage (target, image_);
        if (live_ && rows_ > 0 && rows_ < image_.height ())
          {
            double const y = target.top () + target.height () * rows_ / image_.height ();
            p.fillRect (QRectF {target.left (), y, target.width (), target.bottom () - y}, QColor {tok ("input")});
            p.setPen (QPen {QColor {tok ("accent2")}, 2});
            p.drawLine (QPointF {target.left (), y}, QPointF {target.right (), y});
          }
        p.restore ();
      }
    p.setPen (QPen {QColor {tok ("border")}, 1});
    p.setBrush (Qt::NoBrush);
    p.drawPath (frame);
    if (image_.isNull () && !message_.isEmpty ())
      {
        p.setPen (QColor {tok ("text_secondary")});
        p.drawText (target, Qt::AlignCenter, message_);
      }
  }

private:
  QImage image_;
  int rows_ {0};
  bool live_ {false};
  QString message_;
};

bool SstvPanel::calling_frequency (QString const& band, qint64& hz, bool& lsb)
{
  for (auto const& c : calling)
    {
      if (band == c.band) {hz = c.hz; lsb = c.lsb; return true;}
    }
  return false;
}

SstvPanel::SstvPanel (QSettings * settings, QString const& folder, QWidget * parent)
  : QWidget {parent}
  , settings_ {settings}
  , folder_ {folder}
{
  setObjectName ("sstvPanel");
  setAttribute (Qt::WA_StyledBackground);
  setProperty ("w11_keep", true);
  QDir {}.mkpath (folder_);
  build ();
  restyle ();

  connect (&decoder_, &sstv::Decoder::started, this, &SstvPanel::picture_started);
  connect (&decoder_, &sstv::Decoder::progress, this, &SstvPanel::picture_progress);
  connect (&decoder_, &sstv::Decoder::finished, this, &SstvPanel::picture_finished);
  connect (&decoder_, &sstv::Decoder::call_heard, this, &SstvPanel::call_heard);

  settings_->beginGroup ("SSTV");
  auto_start_->setChecked (settings_->value ("AutoStart", true).toBool ());
  slant_->setChecked (settings_->value ("SlantFix", true).toBool ());
  save_all_->setChecked (settings_->value ("SaveAll", true).toBool ());
  tx_mode_->setCurrentText (settings_->value ("TxMode", "Martin M1").toString ());
  template_->setCurrentIndex (settings_->value ("Template", 0).toInt ());
  report_->setText (settings_->value ("Report", "595").toString ());
  text_->setText (settings_->value ("Text", "").toString ());
  picture_path_ = settings_->value ("Picture").toString ();
  settings_->endGroup ();
  decoder_.set_auto_start (auto_start_->isChecked ());
  decoder_.set_slant_fix (slant_->isChecked ());
  picture_button_->setText (picture_path_.isEmpty () ? tr ("KF0KYL Digi logo") : QFileInfo {picture_path_}.fileName ());
  load_gallery ();
  update_preview ();
  update_send_text ();
}

SstvPanel::~SstvPanel ()
{
  save_settings ();
}

void SstvPanel::save_settings () const
{
  settings_->beginGroup ("SSTV");
  settings_->setValue ("AutoStart", auto_start_->isChecked ());
  settings_->setValue ("SlantFix", slant_->isChecked ());
  settings_->setValue ("SaveAll", save_all_->isChecked ());
  settings_->setValue ("TxMode", tx_mode_->currentText ());
  settings_->setValue ("Template", template_->currentIndex ());
  settings_->setValue ("Report", report_->text ());
  settings_->setValue ("Text", text_->text ());
  settings_->setValue ("Picture", picture_path_);
  settings_->endGroup ();
}

void SstvPanel::build ()
{
  auto * outer = new QHBoxLayout {this};
  outer->setContentsMargins (10, 6, 10, 6);
  outer->setSpacing (10);

  // ---- receive
  auto * rx = card (this);
  auto * rxl = new QVBoxLayout {rx};
  rxl->setContentsMargins (16, 12, 16, 12);
  rxl->setSpacing (10);
  auto * head = new QHBoxLayout;
  head->addWidget (caption (tr ("SSTV Receive"), rx));
  mode_label_ = new QLabel {tr ("Listening"), rx};
  mode_label_->setObjectName ("muted");
  head->addSpacing (8);
  head->addWidget (mode_label_);
  head->addStretch (1);
  auto_start_ = chip (tr ("Auto start"), tr ("Start a picture as soon as its header (VIS code) is heard"), rx);
  slant_ = chip (tr ("Slant fix"), tr ("Straighten pictures that lean because the sound cards' clocks differ"), rx);
  save_all_ = chip (tr ("Save all"), tr ("Save every picture received to the gallery"), rx);
  head->addWidget (auto_start_);
  head->addWidget (slant_);
  head->addWidget (save_all_);
  rxl->addLayout (head);
  view_ = new PictureView {rx};
  view_->set_message (tr ("Listening for pictures…"));
  rxl->addWidget (view_, 1);
  auto * info = new QHBoxLayout;
  from_ = field (rx);
  signal_ = field (rx);
  info->addWidget (from_);
  info->addWidget (signal_);
  auto * prog = new QVBoxLayout;
  status_ = new QLabel {rx};
  status_->setObjectName ("muted");
  progress_ = new QProgressBar {rx};
  progress_->setTextVisible (false);
  progress_->setFixedHeight (7);
  prog->addWidget (status_);
  prog->addWidget (progress_);
  info->addLayout (prog, 1);
  rxl->addLayout (info);
  auto * rxb = new QHBoxLayout;
  auto * stop = new QPushButton {tr ("Stop"), rx};
  auto * save = new QPushButton {tr ("Save"), rx};
  auto * log = new QPushButton {tr ("Log QSO → QRZ"), rx};
  auto * reply = new QPushButton {tr ("Reply with picture"), rx};
  reply->setObjectName ("accent");
  for (auto * b : {stop, save, log, reply}) rxb->addWidget (b, 1);
  rxl->addLayout (rxb);
  outer->addWidget (rx, 3);

  // ---- right column
  auto * right = new QVBoxLayout;
  right->setSpacing (10);
  outer->addLayout (right, 2);

  auto * strip = card (this);
  auto * sl = new QHBoxLayout {strip};
  sl->setContentsMargins (12, 8, 12, 8);
  mode_combo_ = new QComboBox {strip};
  mode_combo_->setObjectName ("modeCombo");
  mode_combo_->setToolTip (tr ("Pick another mode to leave SSTV"));
  freq_ = new QLabel {strip};
  freq_->setObjectName ("freq");
  band_combo_ = new QComboBox {strip};
  band_combo_->setToolTip (tr ("Go to the SSTV calling frequency on this band"));
  for (auto const& c : calling) band_combo_->addItem (QString {"%1  %2"}.arg (c.band).arg (c.hz / 1e6, 0, 'f', 3), c.band);
  auto * strip_log = new QPushButton {tr ("Log QSO"), strip};
  tune_ = new QPushButton {tr ("Tune"), strip};
  tune_->setCheckable (true);
  auto * halt_button = new QPushButton {tr ("Halt Tx"), strip};
  halt_button->setObjectName ("halt");
  sl->addWidget (mode_combo_);
  sl->addWidget (freq_);
  sl->addWidget (band_combo_);
  sl->addStretch (1);
  sl->addWidget (strip_log);
  sl->addWidget (tune_);
  sl->addWidget (halt_button);
  right->addWidget (strip);

  auto * gallery = card (this);
  auto * gl = new QVBoxLayout {gallery};
  gl->setContentsMargins (14, 10, 14, 10);
  auto * gh = new QHBoxLayout;
  gh->addWidget (caption (tr ("Gallery"), gallery));
  gh->addStretch (1);
  gallery_count_ = new QLabel {gallery};
  gallery_count_->setObjectName ("muted");
  auto * open = new QPushButton {tr ("Open folder"), gallery};
  open->setObjectName ("link");
  gh->addWidget (gallery_count_);
  gh->addWidget (open);
  gl->addLayout (gh);
  gallery_row_ = new QHBoxLayout;
  gallery_row_->setSpacing (6);
  gl->addLayout (gallery_row_);
  right->addWidget (gallery);

  auto * heard = card (this);
  auto * hl = new QVBoxLayout {heard};
  hl->setContentsMargins (14, 10, 14, 10);
  auto * hh = new QHBoxLayout;
  hh->addWidget (caption (tr ("SSTV Stations Heard"), heard));
  hh->addStretch (1);
  auto * hnote = new QLabel {tr ("From the ID sent after each picture"), heard};
  hnote->setObjectName ("muted");
  hh->addWidget (hnote);
  hl->addLayout (hh);
  heard_ = new QTableWidget {0, 5, heard};
  heard_->setHorizontalHeaderLabels ({tr ("Call"), tr ("Country"), tr ("km"), tr ("Mode"), tr ("UTC")});
  heard_->verticalHeader ()->hide ();
  heard_->setShowGrid (false);
  heard_->setEditTriggers (QAbstractItemView::NoEditTriggers);
  heard_->setSelectionBehavior (QAbstractItemView::SelectRows);
  heard_->setSelectionMode (QAbstractItemView::SingleSelection);
  heard_->horizontalHeader ()->setSectionResizeMode (QHeaderView::ResizeToContents);
  heard_->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::Stretch);
  heard_->horizontalHeader ()->setDefaultAlignment (Qt::AlignLeft);
  heard_->setToolTip (tr ("Double-click a station to reply to it"));
  heard_->setMinimumHeight (110);
  hl->addWidget (heard_);
  right->addWidget (heard, 1);

  auto * tx = card (this);
  auto * tl = new QVBoxLayout {tx};
  tl->setContentsMargins (14, 10, 14, 12);
  tl->addWidget (caption (tr ("SSTV Transmit"), tx));
  auto * trow = new QHBoxLayout;
  preview_ = new QLabel {tx};
  preview_->setFixedSize (250, 200);
  preview_->setAlignment (Qt::AlignTop | Qt::AlignLeft);
  trow->addWidget (preview_);
  auto * form = new QGridLayout;
  form->setHorizontalSpacing (8);
  form->setVerticalSpacing (6);
  template_ = new QComboBox {tx};
  template_->addItems ({tr ("CQ"), tr ("Reply with report"), tr ("Thanks and 73"), tr ("Picture only")});
  tx_mode_ = new QComboBox {tx};
  for (auto const& m : sstv::modes ())
    {
      tx_mode_->addItem (m.name);
      tx_mode_->setItemData (tx_mode_->count () - 1, tr ("%1 s, %2 × %3").arg (qRound (m.seconds ())).arg (m.width).arg (m.height), Qt::ToolTipRole);
    }
  to_ = new QLineEdit {tx};
  to_->setPlaceholderText (tr ("Their call"));
  report_ = new QLineEdit {tx};
  report_->setPlaceholderText ("595");
  text_ = new QLineEdit {tx};
  text_->setPlaceholderText (tr ("Short text, e.g. TNX FB PIC"));
  picture_button_ = new QPushButton {tx};
  picture_button_->setToolTip (tr ("Choose the picture to send"));
  int r = 0;
  auto add = [&] (QString const& label, QWidget * w) {
    auto * l = new QLabel {label, tx};
    l->setObjectName ("muted");
    form->addWidget (l, r, 0);
    form->addWidget (w, r++, 1);
  };
  add (tr ("Template"), template_);
  add (tr ("Mode"), tx_mode_);
  add (tr ("To"), to_);
  add (tr ("Report"), report_);
  add (tr ("Text"), text_);
  add (tr ("Picture"), picture_button_);
  trow->addLayout (form, 1);
  tl->addLayout (trow);
  tl->addStretch (1);
  auto * tb = new QHBoxLayout;
  auto * choose = new QPushButton {tr ("Choose picture…"), tx};
  send_cq_ = new QPushButton {tr ("Send CQ picture"), tx};
  send_ = new QPushButton {tx};
  send_->setObjectName ("send");
  tb->addWidget (choose, 1);
  tb->addWidget (send_cq_, 1);
  tb->addWidget (send_, 1);
  tl->addLayout (tb);
  right->addWidget (tx);

  // ---- wiring
  connect (auto_start_, &QToolButton::toggled, this, [this] (bool on) {decoder_.set_auto_start (on); save_settings ();});
  connect (slant_, &QToolButton::toggled, this, [this] (bool on) {decoder_.set_slant_fix (on); save_settings ();});
  connect (save_all_, &QToolButton::toggled, this, [this] {save_settings ();});
  connect (stop, &QPushButton::clicked, this, [this] {decoder_.stop ();});
  connect (save, &QPushButton::clicked, this, [this] {
      if (!last_picture_.isNull () && last_saved_.isEmpty ()) save_current (last_call_);
    });
  auto do_log = [this] {
    QString call = to_->text ().trimmed ().toUpper ();
    if (call.isEmpty ()) call = last_call_;
    auto const on = picture_on_.isValid () ? picture_on_ : QDateTime::currentDateTimeUtc ();
    Q_EMIT log_qso (call, report_->text ().trimmed (), QString {}, on, QDateTime::currentDateTimeUtc ());
  };
  connect (log, &QPushButton::clicked, this, do_log);
  connect (strip_log, &QPushButton::clicked, this, do_log);
  connect (reply, &QPushButton::clicked, this, [this] {
      if (!last_call_.isEmpty ()) to_->setText (last_call_);
      if (to_->text ().trimmed ().isEmpty ()) {to_->setFocus (); return;}
      template_->setCurrentIndex (1);
      send (false);
    });
  connect (mode_combo_, QOverload<int>::of (&QComboBox::activated), this, [this] (int i) {
      auto const mode = mode_combo_->itemText (i);
      if (mode != "SSTV") Q_EMIT mode_chosen (mode);
      mode_combo_->setCurrentText ("SSTV");
    });
  connect (band_combo_, QOverload<int>::of (&QComboBox::activated), this, [this] (int i) {
      qint64 hz;
      bool lsb;
      if (calling_frequency (band_combo_->itemData (i).toString (), hz, lsb)) Q_EMIT band_chosen (hz, lsb);
    });
  connect (tune_, &QPushButton::toggled, this, [this] (bool on) {Q_EMIT tune (on);});
  connect (halt_button, &QPushButton::clicked, this, [this] {tune_->setChecked (false); Q_EMIT halt ();});
  connect (open, &QPushButton::clicked, this, [this] {QDesktopServices::openUrl (QUrl::fromLocalFile (folder_));});
  connect (heard_, &QTableWidget::cellDoubleClicked, this, [this] (int row, int) {
      to_->setText (heard_->item (row, 0)->text ());
      template_->setCurrentIndex (1);
    });
  connect (choose, &QPushButton::clicked, this, &SstvPanel::choose_picture);
  connect (picture_button_, &QPushButton::clicked, this, &SstvPanel::choose_picture);
  connect (send_, &QPushButton::clicked, this, [this] {
      if (transmitting_) {Q_EMIT halt (); return;}
      send (false);
    });
  connect (send_cq_, &QPushButton::clicked, this, [this] {
      if (transmitting_) {Q_EMIT halt (); return;}
      send (true);
    });
  for (auto * e : {to_, report_, text_})
    {
      connect (e, &QLineEdit::textChanged, this, [this] {update_preview (); update_send_text ();});
      connect (e, &QLineEdit::editingFinished, this, [this] {save_settings ();});
    }
  connect (template_, QOverload<int>::of (&QComboBox::currentIndexChanged), this, [this] {update_preview (); update_send_text (); save_settings ();});
  connect (tx_mode_, QOverload<int>::of (&QComboBox::currentIndexChanged), this, [this] {update_preview (); update_send_text (); save_settings ();});
}

void SstvPanel::restyle ()
{
  auto const accent2 = tok ("accent2");
  bool const lime = accent2.compare ("#7C5CFF", Qt::CaseInsensitive) != 0;
  auto const send_bg = lime ? accent2 : tok ("accent");
  auto const send_fg = lime ? QString {"#000000"} : tok ("accent_text");
  setStyleSheet (QString {
      "#sstvPanel {background: %1;}"
      "QFrame#card {background: %2; border: 1px solid %3; border-radius: 14px;}"
      "QLabel {color: %4; background: transparent;}"
      "QLabel#cap {color: %5; font-weight: 700; letter-spacing: 1px;}"
      "QLabel#muted {color: %6;}"
      "QLabel#field {background: %7; border: 1px solid %3; border-radius: 8px; padding: 5px 10px;}"
      "QLabel#freq {color: %8; font-size: 20pt; padding: 0 6px;}"
      "QToolButton#chip {background: transparent; color: %6; border: 1px solid %3; border-radius: 12px; padding: 3px 12px;}"
      "QToolButton#chip:checked {background: %9; color: %10; border-color: %9; font-weight: 700;}"
      "QToolButton#thumb {background: %7; color: %6; border: 1px solid %3; border-radius: 8px; padding: 3px; font-size: 8pt;}"
      "QToolButton#thumb:hover {border-color: %9;}"
      "QPushButton {background: %11; color: %4; border: 1px solid %3; border-radius: 8px; padding: 6px 12px;}"
      "QPushButton:hover {background: %12;}"
      "QPushButton:checked {background: %9; color: %10;}"
      "QPushButton#accent {background: %9; color: %10; border-color: %9; font-weight: 700;}"
      "QPushButton#send {background: %13; color: %14; border-color: %13; font-weight: 700;}"
      "QPushButton#halt {color: #FF6B6B;}"
      "QPushButton#link {background: transparent; border: none; color: %9; padding: 2px 4px;}"
      "QComboBox, QLineEdit {background: %7; color: %4; border: 1px solid %3; border-radius: 8px; padding: 4px 8px;}"
      "QComboBox#modeCombo {background: %13; color: %14; border-color: %13; font-weight: 700;}"
      "QComboBox QAbstractItemView, QComboBox#modeCombo QAbstractItemView {background: %2; color: %4; font-weight: 400;"
      " selection-background-color: %9; selection-color: %10;}"
      "QTableWidget {background: %2; color: %4; border: none; font-family: monospace;}"
      "QTableWidget::item:selected {background: %9; color: %10;}"
      "QHeaderView::section {background: %2; color: %6; border: none; font-weight: 600; padding: 2px 4px;}"
      "QProgressBar {background: %11; border: none; border-radius: 3px;}"
      "QProgressBar::chunk {border-radius: 3px; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %9, stop:1 %15);}"
    }
    .arg (tok ("bg"), tok ("card"), tok ("border"), tok ("text"), tok ("title"), tok ("text_secondary"), tok ("input"))
    .arg (lime ? accent2 : tok ("accent"), tok ("accent"), tok ("accent_text"), tok ("ctrl"), tok ("ctrl_hover"))
    .arg (send_bg, send_fg, accent2));
  update_preview ();
  view_->update ();
}

void SstvPanel::set_my_call (QString const& call)
{
  my_call_ = call.toUpper ();
  update_preview ();
}

void SstvPanel::set_my_grid (QString const& grid)
{
  my_grid_ = grid;
  update_preview ();
}

void SstvPanel::set_frequency (qint64 hz)
{
  freq_->setText (QString::number (hz / 1e6, 'f', 3));
  for (auto const& c : calling)
    {
      // keep the band list on the band the radio is on
      double const mhz = hz / 1e6, cmhz = c.hz / 1e6;
      if (std::abs (mhz - cmhz) / cmhz < 0.08) band_combo_->setCurrentIndex (band_combo_->findData (c.band));
    }
}

void SstvPanel::set_modes (QStringList const& modes)
{
  mode_combo_->clear ();
  mode_combo_->addItems (modes);
  mode_combo_->setCurrentText ("SSTV");
}

void SstvPanel::set_transmitting (bool on)
{
  transmitting_ = on;
  if (!on) tune_->setChecked (false);
  update_send_text ();
}

void SstvPanel::update_send_text ()
{
  if (transmitting_)
    {
      send_->setText (tr ("Stop sending"));
      send_cq_->setEnabled (false);
      return;
    }
  send_cq_->setEnabled (true);
  auto const to = to_->text ().trimmed ().toUpper ();
  auto const * m = sstv::mode_by_name (tx_mode_->currentText ());
  QString const secs = m ? QString {" · %1 s"}.arg (qRound (m->seconds ())) : QString {};
  send_->setText (to.isEmpty () ? tr ("Send picture") + secs : tr ("Send to %1").arg (to) + secs);
}

void SstvPanel::add_samples (QVector<short> const& samples)
{
  decoder_.add_samples (samples.constData (), samples.size ());
  if (decoder_.receiving ())
    {
      int const q = decoder_.sync_quality ();
      signal_->setText (small (tr ("Sync")) + (q >= 70 ? tr ("good") : q >= 35 ? tr ("fair") : tr ("weak")));
    }
}

void SstvPanel::picture_started (QString const& mode)
{
  picture_on_ = QDateTime::currentDateTimeUtc ();
  last_mode_ = mode;
  last_call_.clear ();
  last_saved_.clear ();
  mode_label_->setText (tr ("%1 · receiving").arg (mode));
  from_->setText (small (tr ("From")) + tr ("waiting for ID"));
  status_->setText (tr ("Started %1 UTC").arg (picture_on_.toString ("hh:mm:ss")));
  progress_->setValue (0);
}

void SstvPanel::picture_progress (int rows, int total)
{
  view_->set_picture (decoder_.picture (), rows, decoder_.receiving ());
  progress_->setMaximum (total);
  progress_->setValue (rows);
  if (decoder_.receiving ())
    {
      status_->setText (tr ("Line %1 of %2 · %3 s left").arg (rows).arg (total).arg (qRound (decoder_.seconds_left ())));
    }
}

void SstvPanel::picture_finished (QImage const& image, QString const& mode, bool complete)
{
  picture_off_ = QDateTime::currentDateTimeUtc ();
  last_picture_ = image;
  view_->set_picture (image, image.height (), false);
  mode_label_->setText (complete ? tr ("%1 · done").arg (mode) : tr ("%1 · stopped").arg (mode));
  status_->setText (complete ? tr ("Received %1 UTC").arg (picture_off_.toString ("hh:mm:ss"))
                             : tr ("Signal lost or stopped %1 UTC").arg (picture_off_.toString ("hh:mm:ss")));
  if (save_all_->isChecked () && (complete || decoder_.lines_done () > image.height () / 2))
    {
      save_current (QString {});
    }
}

void SstvPanel::call_heard (QString const& call)
{
  last_call_ = call;
  QString where;
  if (lookup_)
    {
      auto const place = lookup_ (call);
      if (!place.country.isEmpty ()) where = " · " + place.country;
      if (place.km >= 0) where += QString {" · ~%L1 km"}.arg (qRound (place.km));
    }
  from_->setText (small (tr ("From")) + QString {"<b style='color:%1'>%2</b>%3"}.arg (tok ("accent2"), call, where));
  add_heard (call, last_mode_, picture_off_.isValid () ? picture_off_ : QDateTime::currentDateTimeUtc ());
  // the picture was saved before its ID came in: put the call in its name
  if (!last_saved_.isEmpty ())
    {
      QFileInfo const fi {last_saved_};
      auto const renamed = fi.absolutePath () + '/' + fi.completeBaseName () + '_' + QString {call}.replace ('/', '-') + ".png";
      if (QFile::rename (last_saved_, renamed)) last_saved_ = renamed;
      load_gallery ();
    }
  Q_EMIT station_heard (call);
}

void SstvPanel::save_current (QString const& call)
{
  if (last_picture_.isNull ()) return;
  auto name = QString {"%1_%2"}.arg (picture_off_.toString ("yyyyMMdd_HHmmss"), QString {last_mode_}.replace (' ', '-'));
  if (!call.isEmpty ()) name += '_' + QString {call}.replace ('/', '-');
  auto const path = folder_ + '/' + name + ".png";
  if (last_picture_.save (path)) last_saved_ = path;
  load_gallery ();
}

void SstvPanel::load_gallery ()
{
  while (auto * item = gallery_row_->takeAt (0))
    {
      delete item->widget ();
      delete item;
    }
  QDir const dir {folder_};
  auto const files = dir.entryInfoList ({"*.png", "*.jpg"}, QDir::Files, QDir::Time);
  int const today = int (std::count_if (files.begin (), files.end (), [] (QFileInfo const& f) {
        return f.lastModified ().toUTC ().date () == QDateTime::currentDateTimeUtc ().date ();
      }));
  gallery_count_->setText (tr ("Today · %n picture(s)", "", today));
  for (int i = 0; i < qMin (6, files.size ()); ++i)
    {
      auto const& f = files[i];
      auto * b = new QToolButton;
      b->setObjectName ("thumb");
      b->setToolButtonStyle (Qt::ToolButtonTextUnderIcon);
      b->setIconSize (QSize {92, 72});
      QImage const img {f.absoluteFilePath ()};
      b->setIcon (QPixmap::fromImage (img.scaled (92, 72, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
      // name: date_time_mode[_call]
      auto const parts = f.completeBaseName ().split ('_');
      QString label = parts.size () > 3 ? QString {parts[3]}.replace ('-', '/') : (parts.size () > 2 && parts[2] == "sent" ? tr ("sent") : tr ("no ID"));
      if (parts.size () > 1 && parts[1].size () >= 4) label += " · " + parts[1].left (2) + ':' + parts[1].mid (2, 2);
      b->setText (label);
      b->setToolTip (f.fileName ());
      auto const path = f.absoluteFilePath ();
      connect (b, &QToolButton::clicked, this, [this, path] {show_gallery_picture (path);});
      gallery_row_->addWidget (b);
    }
  gallery_row_->addStretch (1);
}

void SstvPanel::show_gallery_picture (QString const& path)
{
  if (decoder_.receiving ()) return;      // don't cover a picture coming in
  QImage const img {path};
  if (img.isNull ()) return;
  view_->set_picture (img, img.height (), false);
  mode_label_->setText (QFileInfo {path}.fileName ());
}

void SstvPanel::add_heard (QString const& call, QString const& mode, QDateTime const& when)
{
  for (int r = 0; r < heard_->rowCount (); ++r)
    {
      if (heard_->item (r, 0)->text () == call) {heard_->removeRow (r); break;}
    }
  heard_->insertRow (0);
  Place place;
  if (lookup_) place = lookup_ (call);
  auto * c = new QTableWidgetItem {call};
  c->setForeground (QColor {tok ("accent2")});
  heard_->setItem (0, 0, c);
  heard_->setItem (0, 1, new QTableWidgetItem {place.country});
  heard_->setItem (0, 2, new QTableWidgetItem {place.km >= 0 ? QString {"%L1"}.arg (qRound (place.km)) : QString {}});
  heard_->setItem (0, 3, new QTableWidgetItem {mode});
  heard_->setItem (0, 4, new QTableWidgetItem {when.toString ("hh:mm")});
  while (heard_->rowCount () > 50) heard_->removeRow (heard_->rowCount () - 1);
}

void SstvPanel::choose_picture ()
{
  auto const path = QFileDialog::getOpenFileName (this, tr ("Picture to send"),
      picture_path_.isEmpty () ? QDir::homePath () : QFileInfo {picture_path_}.absolutePath (),
      tr ("Pictures (*.png *.jpg *.jpeg *.bmp *.gif)"));
  if (path.isEmpty ()) return;
  picture_path_ = path;
  picture_button_->setText (QFileInfo {path}.fileName ());
  save_settings ();
  update_preview ();
}

QImage SstvPanel::compose () const
{
  auto const * mode = sstv::mode_by_name (tx_mode_->currentText ());
  int const w = mode ? mode->width : 320, h = mode ? mode->height : 256;
  QImage out {w, h, QImage::Format_RGB32};
  QPainter p {&out};
  p.setRenderHint (QPainter::SmoothPixmapTransform);
  QImage src;
  if (!picture_path_.isEmpty ()) src.load (picture_path_);
  if (src.isNull ())
    {
      // the default: the logo on a dark blue-to-green background
      QLinearGradient g {0, 0, 0, double (h)};
      g.setColorAt (0, QColor {"#06121F"});
      g.setColorAt (1, QColor {"#0A2A14"});
      p.fillRect (out.rect (), g);
      QImage const logo {":/station_logo.png"};
      if (!logo.isNull ())
        {
          QSizeF const s = QSizeF {logo.size ()}.scaled (QSizeF {w * 0.7, h * 0.5}, Qt::KeepAspectRatio);
          p.drawImage (QRectF {QPointF {(w - s.width ()) / 2., h * 0.27}, s}, logo);
        }
    }
  else
    {
      // fill the picture, cropping what doesn't fit
      QSizeF const s = QSizeF {src.size ()}.scaled (QSizeF (w, h), Qt::KeepAspectRatioByExpanding);
      p.drawImage (QRectF {QPointF {(w - s.width ()) / 2., (h - s.height ()) / 2.}, s}, src);
    }

  int const t = template_->currentIndex ();
  if (3 != t)
    {
      auto const my_call = QString {my_call_}.replace ('0', QChar {0x00D8});
      QFont big = QApplication::font ();
      big.setBold (true);
      big.setPixelSize (qRound (h * 0.12));
      outlined (p, QRectF {w * 0.04, h * 0.03, w * 0.92, h * 0.2}, Qt::AlignLeft, my_call, big, Qt::white);
      auto const to = to_->text ().trimmed ().toUpper ();
      auto const text = text_->text ().trimmed ().toUpper ();
      QString line;
      switch (t)
        {
        case 0: line = QString {"CQ SSTV DE %1 %2"}.arg (my_call_, my_grid_.left (4)); break;
        case 1: line = QString {"%1 UR %2 %3"}.arg (to, report_->text ().trimmed (), text); break;
        case 2: line = QString {"%1 TNX 73 %2"}.arg (to, text); break;
        }
      QFont small_font = big;
      small_font.setPixelSize (qRound (h * 0.065));
      QFontMetricsF fm {small_font};
      while (fm.horizontalAdvance (line.trimmed ()) > w * 0.92 && small_font.pixelSize () > 8)
        {
          small_font.setPixelSize (small_font.pixelSize () - 1);
          fm = QFontMetricsF {small_font};
        }
      outlined (p, QRectF {w * 0.04, h * 0.80, w * 0.92, h * 0.17}, Qt::AlignLeft | Qt::AlignBottom, line.trimmed (), small_font, QColor {"#FFE24A"});
    }
  return out;
}

void SstvPanel::update_preview ()
{
  if (!preview_) return;
  QImage const img = compose ();
  QPixmap pm = QPixmap::fromImage (img.scaled (preview_->size (), Qt::KeepAspectRatio, Qt::SmoothTransformation));
  // rounded corners
  QPixmap rounded {pm.size ()};
  rounded.fill (Qt::transparent);
  QPainter p {&rounded};
  p.setRenderHint (QPainter::Antialiasing);
  QPainterPath path;
  path.addRoundedRect (QRectF {rounded.rect ()}, 8, 8);
  p.setClipPath (path);
  p.drawPixmap (0, 0, pm);
  preview_->setPixmap (rounded);
}

void SstvPanel::send (bool cq)
{
  auto const * mode = sstv::mode_by_name (tx_mode_->currentText ());
  if (!mode) return;
  if (cq) template_->setCurrentIndex (0);
  QImage const picture = compose ();
  // keep a copy of what was sent in the gallery
  picture.save (folder_ + '/' + QDateTime::currentDateTimeUtc ().toString ("yyyyMMdd_HHmmss") + "_sent.png");
  load_gallery ();
  Q_EMIT send_audio (sstv::encode (picture, *mode, my_call_));
}
