#include "SplashScreen.hpp"

#include <QPixmap>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QCoreApplication>

#include "revision_utils.hpp"
#include "pimpl_impl.hpp"

#include "moc_SplashScreen.cpp"

class SplashScreen::impl
{
public:
  impl ()
    : checkbox_ {"Do not show this again"}
  {
    main_layout_.addStretch ();
    main_layout_.addWidget (&checkbox_, 0, Qt::AlignRight);
  }

  QVBoxLayout main_layout_;
  QCheckBox checkbox_;
};

SplashScreen::SplashScreen ()
  : QSplashScreen {QPixmap {":/splash.png"}, Qt::WindowStaysOnTopHint}
{
  setLayout (&m_->main_layout_);
  // the logo fills the top of the splash image; the words go underneath
  showMessage ("<h2>" + QString {QCoreApplication::applicationName () + " v" +
        QCoreApplication::applicationVersion() + " " +
        revision ()}.simplified () + "</h2>"
    "by Carson Coordes, KF0KYL<br />"
    "Based on WSJT-X " WSJTX_BASE_VERSION " by K1JT and the WSJT Development Team.<br />"
    "This modified version is not supported by the WSJT-X team: please<br />"
    "don't send its issue reports to the WSJT-X groups. Licensed under GPL v3.<br /><br /><br />"
    , Qt::AlignBottom | Qt::AlignHCenter, Qt::white);
  connect (&m_->checkbox_, &QCheckBox::stateChanged, [this] (int s) {
      if (Qt::Checked == s) Q_EMIT disabled ();
    });
}

SplashScreen::~SplashScreen ()
{
}
