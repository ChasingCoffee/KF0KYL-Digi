#include "CallsignInfo.hpp"

#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <QEvent>
#include <QFontMetrics>

#include "moc_CallsignInfo.cpp"

namespace
{
  int const small_photo {64};

  // the whole photo, scaled to fit the box, with rounded corners
  QPixmap rounded (QPixmap const& source, QSize const& box)
  {
    if (box.width () < 8 || box.height () < 8) return {};
    auto const scaled = source.scaled (box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap result {scaled.size ()};
    result.fill (Qt::transparent);
    QPainter p {&result};
    p.setRenderHint (QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect (QRectF {QPointF {}, QSizeF {scaled.size ()}}, 10, 10);
    p.setClipPath (path);
    p.drawPixmap (0, 0, scaled);
    return result;
  }

  QPixmap initials (QString const& call, int photo_size)
  {
    // placeholder: first letters of the call on a soft tile
    QPixmap result {photo_size, photo_size};
    result.fill (Qt::transparent);
    QPainter p {&result};
    p.setRenderHint (QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect (0, 0, photo_size, photo_size, 10, 10);
    p.fillPath (path, QColor {"#3A6EA5"});
    auto font = p.font ();
    font.setPixelSize (photo_size * 22 / 64);
    font.setBold (true);
    p.setFont (font);
    p.setPen (Qt::white);
    p.drawText (QRect {0, 0, photo_size, photo_size}, Qt::AlignCenter, call.left (2));
    return result;
  }
}

CallsignInfo::CallsignInfo (QWidget * parent)
  : QFrame {parent}
  , photo_ {new QLabel {this}}
  , title_ {new QLabel {this}}
  , details_ {new QLabel {this}}
  , source_ {new QLabel {this}}
  , qrz_button_ {new QPushButton {tr ("QRZ page"), this}}
{
  setObjectName ("callsign_info");
  setFrameShape (QFrame::NoFrame);
  setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Fixed);

  photo_->setFixedSize (small_photo, small_photo);
  photo_->setAlignment (Qt::AlignCenter);
  photo_->setStyleSheet ("background: transparent;");
  photo_->installEventFilter (this);
  title_->setTextFormat (Qt::RichText);
  details_->setTextFormat (Qt::PlainText);
  details_->setWordWrap (true);
  source_->setStyleSheet ("color: palette(mid); font-size: 11px;");
  for (auto * l : {title_, details_, source_})
    {
      l->setTextInteractionFlags (Qt::TextSelectableByMouse);
    }
  qrz_button_->setToolTip (tr ("Open this station's QRZ.com page in your web browser"));
  qrz_button_->setFocusPolicy (Qt::NoFocus);   // never steal the keyboard while operating
  connect (qrz_button_, &QPushButton::clicked, this, &CallsignInfo::openQrzPage);

  // laid out for the DX box in the Conditions window: the photo beside
  // the call, name and place, then the QRZ page button
  title_->setWordWrap (true);
  auto * text = new QVBoxLayout;
  text->setSpacing (1);
  text->addWidget (title_);
  text->addWidget (details_);
  text->addStretch ();

  auto * top = top_ = new QHBoxLayout;
  top->setSpacing (12);
  top->addWidget (photo_, 0, Qt::AlignTop);
  top->addLayout (text, 1);

  auto * bottom = new QHBoxLayout;
  bottom->setSpacing (8);
  bottom->addWidget (qrz_button_);
  bottom->addWidget (source_, 1);

  auto * column = column_ = new QVBoxLayout {this};
  column->setContentsMargins (0, 0, 0, 0);
  column->setSpacing (8);
  column->addLayout (top);
  column->addLayout (bottom);

  clear ();
}

void CallsignInfo::clear ()
{
  call_.clear ();
  country_.clear ();
  hide ();
}

void CallsignInfo::showCall (QString const& call, QString const& country)
{
  call_ = call.toUpper ();
  country_ = country;
  photo_raw_ = QPixmap {};
  refresh_photo ();
  title_->setText (QString {"<span style='font-size:16px; font-weight:600;'>%1</span>"}.arg (call_.toHtmlEscaped ()));
  details_->setText (country_);
  source_->setText (tr ("Looking up…"));
  qrz_button_->setEnabled (true);
  show ();
}

void CallsignInfo::setInfo (CallsignLookup::Info const& i)
{
  if (!call_.contains (i.call)) return;   // a different station is showing now
  auto title = QString {"<span style='font-size:16px; font-weight:600;'>%1</span>"}.arg (call_.toHtmlEscaped ());
  if (i.name.size ())
    {
      title += QString {"&nbsp;&nbsp;<span style='font-size:16px;'>%1</span>"}.arg (i.name.toHtmlEscaped ());
    }
  if (i.nickname.size () && !i.name.contains (i.nickname, Qt::CaseInsensitive))
    {
      title += QString {"&nbsp;<span style='font-size:13px;'>“%1”</span>"}.arg (i.nickname.toHtmlEscaped ());
    }
  title_->setText (title);

  QStringList place;
  if (i.city.size ()) place << i.city;
  if (i.state.size ()) place << i.state;
  place << (i.country.size () ? i.country : country_);
  QStringList line {place.join (", ")};
  if (i.grid.size ()) line << i.grid.left (6);
  if (i.license_class.size ()) line << tr ("%1 class").arg (i.license_class);
  details_->setText (line.join ("  ·  "));
  source_->setText (i.source);
}

void CallsignInfo::setPhoto (QString const& call, QPixmap const& pixmap)
{
  if (!call_.contains (call) || pixmap.isNull ()) return;
  photo_raw_ = pixmap;
  refresh_photo ();
}

void CallsignInfo::setNotFound (QString const& call, QString const& reason)
{
  if (!call_.contains (call)) return;
  details_->setText (country_);
  source_->setText (reason);
}

void CallsignInfo::openQrzPage ()
{
  if (call_.isEmpty ()) return;
  QDesktopServices::openUrl (QUrl {QString {"https://www.qrz.com/db/%1"}.arg (QString::fromLatin1 (QUrl::toPercentEncoding (call_)))});
}

void CallsignInfo::setBigPhoto (bool big)
{
  if (big == big_) return;
  big_ = big;
  if (big)
    {
      // above the name, as big as the room allows
      top_->removeWidget (photo_);
      photo_->setMinimumSize (1, 1);
      photo_->setMaximumSize (QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
      photo_->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
      column_->insertWidget (0, photo_, 1);
      setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Expanding);
    }
  else
    {
      column_->removeWidget (photo_);
      photo_->setFixedSize (small_photo, small_photo);
      photo_->setSizePolicy (QSizePolicy::Fixed, QSizePolicy::Fixed);
      top_->insertWidget (0, photo_, 0, Qt::AlignTop);
      setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Fixed);
    }
  refresh_photo ();
}

int CallsignInfo::compactHeight () const
{
  // the call and name in 16 px type, then up to two lines of place
  int const text = QFontMetrics {font ()}.lineSpacing () * 2 + 24;
  return qMax (small_photo, text) + 8 + qrz_button_->sizeHint ().height ();
}

void CallsignInfo::refresh_photo ()
{
  if (call_.isEmpty ()) return;
  auto const box = photo_->size ();
  if (!photo_raw_.isNull ()) photo_->setPixmap (rounded (photo_raw_, box));
  else
    {
      int const side = qMin (qMin (box.width (), box.height ()), 150);
      if (side >= 8) photo_->setPixmap (initials (call_, side));
    }
}

bool CallsignInfo::eventFilter (QObject * o, QEvent * e)
{
  // the big photo follows the room it's given
  if (o == photo_ && QEvent::Resize == e->type ()) refresh_photo ();
  return QFrame::eventFilter (o, e);
}
