#include "widegraph.h"
#include <QComboBox>

#include <algorithm>
#include <QApplication>
#include <QSettings>
#include <QDateTime>
#include <QKeyEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QCheckBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QButtonGroup>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QFrame>
#include <QPainter>
#include <QSpinBox>
#include "Win11Theme.hpp"
#include <cmath>
#include <math.h>
#include "ui_widegraph.h"
#include "commons.h"
#include "Configuration.hpp"
#include "MessageBox.hpp"
#include "SettingsGroup.hpp"
#include "moc_widegraph.cpp"

WideGraph::WideGraph(QSettings * settings, QWidget *parent) :
  QDialog(parent),
  ui(new Ui::WideGraph),
  m_settings (settings),
  m_palettes_path {":/Palettes"},
  m_tr0 {0.0},
  m_n {0},
  m_bHaveTransmitted {false},
  m_user_defined {tr ("User Defined")}
{
  ui->setupUi(this);

  setWindowTitle (QApplication::applicationName () + " - " + tr ("Wide Graph"));
  setWindowFlags (Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint);
  setMaximumWidth (MAX_SCREENSIZE);
  setMaximumHeight (880);

  ui->widePlot->setCursor(Qt::CrossCursor);
  ui->widePlot->setMaximumHeight(800);
  ui->widePlot->setCurrent(false);
  ui->cbControls->setCursor(Qt::ArrowCursor);
  ui->cbBars->setCursor(Qt::ArrowCursor);

  // The controls under the plot are wider than a narrow waterfall box;
  // let them scroll sideways instead of stopping the box from shrinking.
  if (auto * box = qobject_cast<QVBoxLayout *> (layout ()))
    {
      int const index = box->indexOf (ui->controls_widget);
      m_controlsScroll = new QScrollArea {this};
      m_controlsScroll->setObjectName ("controls_scroll");
      m_controlsScroll->setFrameShape (QFrame::NoFrame);
      m_controlsScroll->setWidgetResizable (true);
      m_controlsScroll->setVerticalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
      m_controlsScroll->setHorizontalScrollBarPolicy (Qt::ScrollBarAsNeeded);
      m_controlsScroll->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Fixed);
      box->removeWidget (ui->controls_widget);
      m_controlsScroll->setWidget (ui->controls_widget);
      box->insertWidget (index < 0 ? box->count () : index, m_controlsScroll);
    }

  // the show/hide controls switch over the waterfall is a sliding switch
  // now, wider than the old check box
  ui->cbControls->setMaximumWidth (QWIDGETSIZE_MAX);
  ui->cbControls->setGeometry (3, 3, 34, 20);

  // "Fit" keeps the whole band visible however narrow the waterfall gets
  m_fitBand = new QCheckBox {tr ("Fit"), ui->controls_widget};
  m_fitBand->setObjectName ("cbFitBand");
  m_fitBand->setToolTip (tr ("Squeeze the waterfall so the whole band always fits its width"));
  {
    int const index = ui->verticalLayout_3->indexOf (ui->bppSpinBox);
    ui->verticalLayout_3->removeWidget (ui->bppSpinBox);
    auto * row = new QHBoxLayout;
    row->setSpacing (6);
    ui->bppSpinBox->setMinimumWidth (ui->bppSpinBox->sizeHint ().width ());
    row->addWidget (ui->bppSpinBox, 1);
    row->addWidget (m_fitBand);
    ui->verticalLayout_3->insertLayout (index < 0 ? 0 : index, row);
  }
  // how the waterfall is drawn, next to the palette
  m_displayStyle = new QComboBox {ui->controls_widget};
  m_displayStyle->setObjectName ("displayStyleComboBox");
  m_displayStyle->addItem (tr ("Classic"));
  m_displayStyle->addItem (tr ("SDR"));
  m_displayStyle->addItem (tr ("3D"));
  m_displayStyle->addItem (tr ("Both"));
  m_displayStyle->setToolTip (tr ("<p>How the waterfall is drawn:</p>"
                                  "<p><b>Classic:</b> the waterfall with the spectrum under it.</p>"
                                  "<p><b>SDR:</b> a big spectrum with a grid and peak hold, the waterfall under it. "
                                  "The Spec % setting sets how tall the spectrum is.</p>"
                                  "<p><b>3D:</b> signals stand up as peaks, newest in front, older rows going back in time.</p>"
                                  "<p><b>Both:</b> 3D over the waterfall.</p>"));
  ui->horizontalLayout->insertWidget (0, m_displayStyle);
  ui->adjust_palette_push_button->setMaximumWidth (QWIDGETSIZE_MAX);   // room for its text with the rounder buttons
  // the line across the waterfall at the start of each period
  m_periodLines = new QCheckBox {tr ("Lines"), ui->controls_widget};
  m_periodLines->setObjectName ("cbPeriodLines");
  m_periodLines->setToolTip (tr ("Draw a line across the waterfall at the start of each period (red after you transmitted)"));
  ui->horizontalLayout_2->addWidget (m_periodLines);
  connect (m_displayStyle, static_cast<void (QComboBox::*) (int)> (&QComboBox::currentIndexChanged), this, [this] (int i) {
      ui->widePlot->setDisplayStyle (i);
    });
  connect (m_periodLines, &QCheckBox::toggled, this, [this] (bool on) {
      ui->widePlot->setPeriodLines (on);
    });
  m_fitTimer = new QTimer {this};
  m_fitTimer->setSingleShot (true);
  m_fitTimer->setInterval (150);
  connect (m_fitTimer, &QTimer::timeout, this, &WideGraph::fit_band);
  connect (m_fitBand, &QCheckBox::toggled, this, [this] (bool on) {
      ui->bppSpinBox->setEnabled (!on);
      if (on) fit_band ();
    });

  connect(ui->widePlot, SIGNAL(freezeDecode1(int)),this,
          SLOT(wideFreezeDecode(int)));
  connect (ui->widePlot, &CPlotter::labelDoubleClicked, this, &WideGraph::labelDoubleClicked);

  connect(ui->widePlot, SIGNAL(setFreq1(int,int)),this,
          SLOT(setFreq2(int,int)));

  {
    //Restore user's settings
    SettingsGroup g {m_settings, "WideGraph"};
    restoreGeometry (m_settings->value ("geometry", saveGeometry ()).toByteArray ());
    ui->widePlot->setPlotZero(m_settings->value("PlotZero", 0).toInt());
    ui->widePlot->setPlotGain(m_settings->value("PlotGain", 0).toInt());
    ui->widePlot->setPlot2dGain(m_settings->value("Plot2dGain", 0).toInt());
    ui->widePlot->setPlot2dZero(m_settings->value("Plot2dZero", 0).toInt());
    ui->zeroSlider->setValue(ui->widePlot->plotZero());
    ui->gainSlider->setValue(ui->widePlot->plotGain());
    ui->gain2dSlider->setValue(ui->widePlot->plot2dGain());
    ui->zero2dSlider->setValue(ui->widePlot->plot2dZero());
    m_timestamp = 1; int itstamp=m_settings->value("Timestamp",1).toInt();
    QString ststamp=m_settings->value("Timestamp","1").toString();
    if(ststamp == "0" || ststamp == "1" || ststamp == "2") m_timestamp = itstamp;
    ui->timestampComboBox->setCurrentIndex(m_timestamp); ui->widePlot->setTimestamp(m_timestamp);
    m_bars=m_settings->value("Bars",true).toBool();
    ui->cbBars->setChecked(m_bars);
    ui->widePlot->setBars(m_bars);
    m_freq=m_settings->value("Freq",true).toBool();
    ui->cbFreq->setChecked(m_freq);
    ui->widePlot->showFreq(m_freq);
    int n = m_settings->value("BinsPerPixel",5).toInt();
    m_bFlatten=m_settings->value("Flatten",true).toBool();
    m_bRef=m_settings->value("UseRef",false).toBool();
    ui->cbFlatten->setChecked(m_bFlatten);
    ui->widePlot->setFlatten(m_bFlatten,m_bRef);
    ui->cbRef->setChecked(m_bRef);
    ui->widePlot->setBreadth(m_settings->value("PlotWidth",1000).toInt());
    ui->bppSpinBox->setValue(n);
    m_nsmo=m_settings->value("SmoothYellow",1).toInt();
    ui->smoSpinBox->setValue(m_nsmo);
    m_Percent2DScreen=m_settings->value("Percent2D",30).toInt();
    m_waterfallAvg = m_settings->value("WaterfallAvg",2).toInt();
    ui->waterfallAvgSpinBox->setValue(m_waterfallAvg);
    ui->widePlot->setWaterfallAvg(m_waterfallAvg);
    ui->widePlot->setCurrent(m_settings->value("Current",false).toBool());
    ui->widePlot->setCumulative(m_settings->value("Cumulative",true).toBool());
    ui->widePlot->setLinearAvg(m_settings->value("LinearAvg",false).toBool());
    ui->widePlot->setReference(m_settings->value("Reference",false).toBool());
    ui->widePlot->setQ65_Sync(m_settings->value("Q65_Sync",false).toBool());
    ui->widePlot->setTotalPower(m_settings->value("TotalPower",false).toBool());
    if(ui->widePlot->current()) ui->spec2dComboBox->setCurrentIndex(0);
    if(ui->widePlot->cumulative()) ui->spec2dComboBox->setCurrentIndex(1);
    if(ui->widePlot->linearAvg()) ui->spec2dComboBox->setCurrentIndex(2);
    if(ui->widePlot->Reference()) ui->spec2dComboBox->setCurrentIndex(3);
    if(ui->widePlot->Q65_Sync()) ui->spec2dComboBox->setCurrentIndex(4);
    if(ui->widePlot->TotalPower()) ui->spec2dComboBox->setCurrentIndex(5);
    int nbpp=m_settings->value("BinsPerPixel",4).toInt();
    ui->widePlot->setBinsPerPixel(nbpp);
    ui->sbPercent2dPlot->setValue(m_Percent2DScreen);
    ui->widePlot->setStartFreq(m_settings->value("StartFreq",0).toInt());
    ui->fStartSpinBox->setValue(ui->widePlot->startFreq());
    m_waterfallPalette=m_settings->value("WaterfallPalette","Default").toString();
    m_userPalette = WFPalette {m_settings->value("UserPalette").value<WFPalette::Colours> ()};
    m_fMinPerBand = m_settings->value ("FminPerBand").toHash ();
    setRxRange ();
    m_controlsScroll->setVisible(!m_settings->value("HideControls",false).toBool());
    m_fitBand->setChecked (m_settings->value ("FitBand", true).toBool ());
    m_displayStyle->setCurrentIndex (qBound (0, m_settings->value ("DisplayStyle", 1).toInt (), 3));
    ui->widePlot->setDisplayStyle (m_displayStyle->currentIndex ());
    m_periodLines->setChecked (m_settings->value ("PeriodLines", true).toBool ());
    ui->widePlot->setPeriodLines (m_periodLines->isChecked ());
    ui->bppSpinBox->setEnabled (!m_fitBand->isChecked ());
    ui->cbControls->setChecked(!m_settings->value("HideControls",false).toBool());
  }

  int index=0;
  for (QString const& file:
         m_palettes_path.entryList(QDir::NoDotAndDotDot |
                                   QDir::System | QDir::Hidden |
                                   QDir::AllDirs | QDir::Files,
                                   QDir::DirsFirst)) {
    QString t=file.mid(0,file.length()-4);
    ui->paletteComboBox->addItem(t);
    if(t==m_waterfallPalette) ui->paletteComboBox->setCurrentIndex(index);
    index++;
  }
  ui->paletteComboBox->addItem (m_user_defined);
  if (m_user_defined == m_waterfallPalette) ui->paletteComboBox->setCurrentIndex(index);
  readPalette ();
  build_toolbar ();
}

// the bar over the waterfall: the view, the colours, three sliders, the
// calls on signals; More opens the rest of the controls under it
void WideGraph::build_toolbar ()
{
  auto * box = qobject_cast<QVBoxLayout *> (layout ());
  if (!box) return;
  m_toolbar = new QWidget {this};
  m_toolbar->setObjectName ("wfToolbar");
  auto * h = new QHBoxLayout {m_toolbar};
  h->setContentsMargins (6, 4, 6, 4);
  h->setSpacing (8);
  QColor const accent {Win11Theme::token ("accent")};
  m_toolbar->setStyleSheet (QString {
      "QPushButton#wfView {border: none; border-radius: 7px; padding: 4px 11px; background: transparent; color: %2;}"
      "QPushButton#wfView:checked {background: %1; color: white;}"
      "QFrame#wfSeg {border: 1px solid %3; border-radius: 9px; background: rgba(127, 127, 127, 28);}"
      "QPushButton#wfSwatch {border: 2px solid transparent; border-radius: 6px; padding: 1px; background: transparent;}"
      "QPushButton#wfSwatch:checked {border-color: %1;}"
      "QLabel#wfLabel {color: %2;}"
      "QPushButton#wfPill {border-radius: 11px; padding: 3px 12px; font-weight: 600;}"}
    .arg (accent.name ())
    .arg (Win11Theme::token ("text_secondary"))
    .arg (Win11Theme::token ("border"))
    .arg (QString {})
    .arg (Win11Theme::token ("text")));
  m_toolbar->setProperty ("w11_keep", true);

  // one pill opens and closes everything on the bar, More included
  m_tbPill = new QPushButton {m_toolbar};
  m_tbPill->setObjectName ("wfPill");
  m_tbPill->setCheckable (true);
  m_tbPill->setFocusPolicy (Qt::NoFocus);
  m_tbPill->setCursor (Qt::PointingHandCursor);
  h->addWidget (m_tbPill);

  // the view
  auto * seg = new QFrame {m_toolbar};
  seg->setObjectName ("wfSeg");
  auto * sh = new QHBoxLayout {seg};
  sh->setContentsMargins (2, 2, 2, 2);
  sh->setSpacing (0);
  m_viewGroup = new QButtonGroup {seg};
  struct V {int style; QString text; QString tip;};
  QVector<V> const views {
    {0, tr ("Classic"), tr ("The waterfall with the spectrum under it, as in WSJT-X")},
    {1, tr ("Waterfall"), tr ("A glowing spectrum over the waterfall")},
    {2, tr ("3D"), tr ("Signals stand up as peaks: the newest in front, the last minute or so behind them")},
    {3, tr ("Both"), tr ("3D over the waterfall")},
  };
  for (auto const& v : views)
    {
      auto * b = new QPushButton {v.text, seg};
      b->setObjectName ("wfView");
      b->setCheckable (true);
      b->setToolTip (v.tip);
      b->setFocusPolicy (Qt::NoFocus);
      b->setCursor (Qt::PointingHandCursor);
      m_viewGroup->addButton (b, v.style);
      sh->addWidget (b);
    }
  h->addWidget (seg);
  connect (m_viewGroup, static_cast<void (QButtonGroup::*) (QAbstractButton *)> (&QButtonGroup::buttonClicked), this, [this] (QAbstractButton * b) {
      m_displayStyle->setCurrentIndex (m_viewGroup->id (b));
    });

  // the colours: a few favourites; every palette is under More
  auto * colours = new QLabel {tr ("Colours"), m_toolbar};
  colours->setObjectName ("wfLabel");
  h->addSpacing (4);
  h->addWidget (colours);
  m_paletteGroup = new QButtonGroup {m_toolbar};
  m_paletteGroup->setExclusive (false);
  for (auto const& name : {QString {"Digi"}, QString {"Nebula"}, QString {"Default"}})
    {
      QVector<QColor> cols;
      try {cols = WFPalette {m_palettes_path.absoluteFilePath (name + ".pal")}.interpolate ();} catch (...) {}
      QPixmap pix {44, 14};
      pix.fill (Qt::transparent);
      {
        QPainter p {&pix};
        p.setRenderHint (QPainter::Antialiasing);
        QLinearGradient g {QPointF {0, 0}, QPointF {44, 0}};
        for (int i = 0; i <= 8 && cols.size (); ++i) g.setColorAt (i / 8., cols.value (qMin (cols.size () - 1, i * (cols.size () - 1) / 8)));
        p.setPen (Qt::NoPen);
        p.setBrush (g);
        p.drawRoundedRect (QRectF {0, 0, 44, 14}, 4, 4);
      }
      auto * b = new QPushButton {m_toolbar};
      b->setObjectName ("wfSwatch");
      b->setIcon (QIcon {pix});
      b->setIconSize (pix.size ());
      b->setCheckable (true);
      b->setFocusPolicy (Qt::NoFocus);
      b->setCursor (Qt::PointingHandCursor);
      b->setToolTip ("Default" == name ? tr ("Classic colours") : name);
      b->setProperty ("palette", name);
      m_paletteGroup->addButton (b);
      h->addWidget (b);
      connect (b, &QPushButton::clicked, this, [this, name] {
          m_waterfallPalette = name;
          ui->paletteComboBox->setCurrentText (name);
          readPalette ();
          replot ();
          sync_toolbar ();
        });
    }

  // gain, floor and speed
  auto slider = [this, h] (QString const& text, QString const& tip, int lo, int hi) {
    auto * l = new QLabel {text, m_toolbar};
    l->setObjectName ("wfLabel");
    auto * s = new QSlider {Qt::Horizontal, m_toolbar};
    s->setRange (lo, hi);
    s->setFixedWidth (90);
    s->setToolTip (tip);
    l->setToolTip (tip);
    h->addSpacing (6);
    h->addWidget (l);
    h->addWidget (s);
    return s;
  };
  m_tbGain = slider (tr ("Gain"), tr ("How bright signals are on the waterfall"), ui->gainSlider->minimum (), ui->gainSlider->maximum ());
  m_tbFloor = slider (tr ("Floor"), tr ("How bright the background noise is"), ui->zeroSlider->minimum (), ui->zeroSlider->maximum ());
  m_tbSpeed = slider (tr ("Speed"), tr ("How fast the waterfall moves"), 1, 10);
  connect (m_tbGain, &QSlider::valueChanged, ui->gainSlider, &QSlider::setValue);
  connect (m_tbFloor, &QSlider::valueChanged, ui->zeroSlider, &QSlider::setValue);
  connect (m_tbSpeed, &QSlider::valueChanged, this, [this] (int speed) {
      ui->waterfallAvgSpinBox->setValue (11 - speed);
    });
  h->addStretch ();

  m_tbNames = new QPushButton {tr ("Calls on signals"), m_toolbar};
  m_tbNames->setCheckable (true);
  m_tbNames->setToolTip (tr ("Show each decoded station's call on its signal. Double-click one to call it."));
  h->addWidget (m_tbNames);
  m_tbMore = new QPushButton {tr ("More"), m_toolbar};
  m_tbMore->setCheckable (true);
  m_tbMore->setToolTip (tr ("All the waterfall's settings"));
  h->addWidget (m_tbMore);
  bool open = true;
  {
    SettingsGroup g {m_settings, "WideGraph"};
    m_tbNames->setChecked (m_settings->value ("NamesOnSignals", true).toBool ());
    m_tbMore->setChecked (m_settings->value ("MoreOpen", false).toBool ());
    open = m_settings->value ("ToolbarOpen", true).toBool ();
  }
  ui->widePlot->setShowLabels (m_tbNames->isChecked ());
  connect (m_tbNames, &QPushButton::toggled, this, [this] (bool on) {
      ui->widePlot->setShowLabels (on);
      SettingsGroup g {m_settings, "WideGraph"};
      m_settings->setValue ("NamesOnSignals", on);
    });
  connect (m_tbMore, &QPushButton::toggled, this, [this] (bool on) {
      ui->cbControls->setChecked (on);
      m_controlsScroll->setVisible (on && m_tbPill->isChecked ());
      SettingsGroup g {m_settings, "WideGraph"};
      m_settings->setValue ("MoreOpen", on);
    });
  connect (m_tbPill, &QPushButton::toggled, this, [this] (bool on) {
      for (auto * w : m_toolbar->findChildren<QWidget *> (QString {}, Qt::FindDirectChildrenOnly))
        {
          if (w != m_tbPill) w->setVisible (on);
        }
      m_controlsScroll->setVisible (on && m_tbMore->isChecked ());
      m_tbPill->setText (on ? tr ("Hide %1").arg (QChar {0x25B4}) : tr ("Tools %1").arg (QChar {0x25BE}));
      m_tbPill->setToolTip (on ? tr ("Hide the waterfall's tools") : tr ("Show the waterfall's tools"));
      SettingsGroup g {m_settings, "WideGraph"};
      m_settings->setValue ("ToolbarOpen", on);
    });
  // the old switch over the waterfall gives way to More
  ui->cbControls->hide ();
  m_tbPill->setChecked (!open);
  m_tbPill->setChecked (open);      // sets up what shows either way
  // the view is chosen on the bar; the old list under More goes
  m_displayStyle->hide ();
  box->insertWidget (0, m_toolbar);

  // keep the toolbar in step with the controls under More
  connect (m_displayStyle, static_cast<void (QComboBox::*) (int)> (&QComboBox::currentIndexChanged), this, &WideGraph::sync_toolbar);
  connect (ui->gainSlider, &QSlider::valueChanged, this, &WideGraph::sync_toolbar);
  connect (ui->zeroSlider, &QSlider::valueChanged, this, &WideGraph::sync_toolbar);
  connect (ui->waterfallAvgSpinBox, static_cast<void (QSpinBox::*) (int)> (&QSpinBox::valueChanged), this, &WideGraph::sync_toolbar);
  connect (ui->paletteComboBox, static_cast<void (QComboBox::*) (int)> (&QComboBox::activated), this, &WideGraph::sync_toolbar);
  sync_toolbar ();
}

void WideGraph::sync_toolbar ()
{
  if (!m_toolbar) return;
  for (auto * b : m_viewGroup->buttons ()) b->setChecked (m_viewGroup->id (b) == m_displayStyle->currentIndex ());
  for (auto * b : m_paletteGroup->buttons ()) b->setChecked (b->property ("palette").toString () == m_waterfallPalette);
  for (auto * s : {m_tbGain, m_tbFloor, m_tbSpeed}) s->blockSignals (true);
  m_tbGain->setValue (ui->gainSlider->value ());
  m_tbFloor->setValue (ui->zeroSlider->value ());
  m_tbSpeed->setValue (qBound (1, 11 - ui->waterfallAvgSpinBox->value (), 10));
  for (auto * s : {m_tbGain, m_tbFloor, m_tbSpeed}) s->blockSignals (false);
}

void WideGraph::addDecodeLabel (int freq, QString const& call, int snr, QColor const& colour, QString const& tip, QString const& period)
{
  ui->widePlot->addDecodeLabel (CPlotter::DecodeLabel {freq, call, snr, colour, tip}, period);
}

WideGraph::~WideGraph ()
{
}

void WideGraph::closeEvent (QCloseEvent * e)
{
  saveSettings ();
  QDialog::closeEvent (e);
}

void WideGraph::saveSettings()                                           //saveSettings
{
  SettingsGroup g {m_settings, "WideGraph"};
  if (isWindow ()) m_settings->setValue ("geometry", saveGeometry ());   // keep floating geometry while embedded
  m_settings->setValue ("PlotZero", ui->widePlot->plotZero());
  m_settings->setValue ("PlotGain", ui->widePlot->plotGain());
  m_settings->setValue ("Plot2dGain", ui->widePlot->plot2dGain());
  m_settings->setValue ("Plot2dZero", ui->widePlot->plot2dZero());
  m_settings->setValue ("PlotWidth", ui->widePlot->plotWidth ());
  m_settings->setValue ("BinsPerPixel", ui->bppSpinBox->value ());
  m_settings->setValue ("SmoothYellow", ui->smoSpinBox->value ());
  m_settings->setValue ("Percent2D",m_Percent2DScreen);
  m_settings->setValue ("WaterfallAvg", ui->waterfallAvgSpinBox->value ());
  m_settings->setValue ("Current", ui->widePlot->current());
  m_settings->setValue ("Cumulative", ui->widePlot->cumulative());
  m_settings->setValue ("LinearAvg", ui->widePlot->linearAvg());
  m_settings->setValue ("Reference", ui->widePlot->Reference());
  m_settings->setValue ("Q65_Sync", ui->widePlot->Q65_Sync());
  m_settings->setValue ("TotalPower", ui->widePlot->TotalPower());
  m_settings->setValue ("BinsPerPixel", ui->widePlot->binsPerPixel ());
  m_settings->setValue ("StartFreq", ui->widePlot->startFreq ());
  m_settings->setValue ("WaterfallPalette", m_waterfallPalette);
  m_settings->setValue ("UserPalette", QVariant::fromValue (m_userPalette.colours ()));
  m_settings->setValue("Flatten",m_bFlatten);
  m_settings->setValue("UseRef",m_bRef);
  m_settings->setValue ("HideControls", m_controlsScroll->isHidden ());
  m_settings->setValue ("FitBand", m_fitBand->isChecked ());
  m_settings->setValue ("Timestamp",m_timestamp);
  m_settings->setValue ("DisplayStyle", m_displayStyle->currentIndex ());
  m_settings->setValue ("PeriodLines", m_periodLines->isChecked ());
  m_settings->setValue ("Bars", m_bars);
  m_settings->setValue ("Freq", m_freq);
  m_settings->setValue ("FminPerBand", m_fMinPerBand);
}

void WideGraph::drawRed(int ia, int ib)
{
  ui->widePlot->drawRed(ia,ib,m_swide);
}

void WideGraph::dataSink2(float s[], float df3, int ihsym, int ndiskdata, float pdB)  //dataSink2
{
  static float splot[NSMAX];
  int nbpp = ui->widePlot->binsPerPixel();

  if(ui->widePlot->TotalPower()) ui->widePlot->drawTotalPower(pdB);
//Average spectra over specified number, m_waterfallAvg
  if (m_n==0) {
    for (int i=0; i<NSMAX; i++)
      splot[i]=s[i];
  } else {
    for (int i=0; i<NSMAX; i++)
      splot[i] += s[i];
  }
  m_n++;

  if (m_n>=m_waterfallAvg) {
    for (int i=0; i<NSMAX; i++)
        splot[i] /= m_n;        //Normalize the average
    m_n=0;
    int i=int(ui->widePlot->startFreq()/df3 + 0.5);
    int jz=5000.0/(nbpp*df3);
		if(jz>MAX_SCREENSIZE) jz=MAX_SCREENSIZE;
    m_jz=jz;
    for (int j=0; j<jz; j++) {
      float ss=0.0;
      float smax=0;
      for (int k=0; k<nbpp; k++) {
        float sp=splot[i++];
        ss += sp;
        smax=qMax(smax,sp);
      }
//      m_swide[j]=nbpp*smax;
      m_swide[j]=nbpp*ss;
    }

// Time according to this computer
    qint64 ms = QDateTime::currentMSecsSinceEpoch() % 86400000;
    double tr = fmod(0.001*ms,m_TRperiod);
    if((ndiskdata && ihsym <= m_waterfallAvg) || (!ndiskdata && (tr<m_tr0))) {
      float flagValue=1.0e30;
      if(m_bHaveTransmitted) flagValue=2.0e30;
      for(int i=0; i<MAX_SCREENSIZE; i++) {
        m_swide[i] = flagValue;
      }
      for(int i=0; i<NSMAX; i++) {
        splot[i] = flagValue;
      }
      m_bHaveTransmitted=false;
    }
    m_tr0=tr;
    ui->widePlot->draw(m_swide,true,false);
  }
}

void WideGraph::on_bppSpinBox_valueChanged(int n)                            //bpp
{
  ui->widePlot->setBinsPerPixel(n);
}

void WideGraph::on_waterfallAvgSpinBox_valueChanged(int n)                  //Navg
{
  m_waterfallAvg = n;
  ui->widePlot->setWaterfallAvg(n);
}

void WideGraph::keyPressEvent(QKeyEvent *e)                                 //F11, F12
{  
  switch(e->key())
  {
  int n;
  case Qt::Key_F11:
    n=11;
    if(e->modifiers() & Qt::ControlModifier) n+=100;
    emit f11f12(n);
    break;
  case Qt::Key_F12:
    n=12;
    if(e->modifiers() & Qt::ControlModifier) n+=100;
    emit f11f12(n);
    break;
  default:
    if (isWindow ())
      {
        QDialog::keyPressEvent (e);
      }
    else
      {
        // embedded in the main window: don't let Esc hide the panel,
        // pass unhandled keys up to the main window instead
        QWidget::keyPressEvent (e);
      }
  }
}

void WideGraph::restoreWindowGeometry ()
{
  SettingsGroup g {m_settings, "WideGraph"};
  auto const geometry = m_settings->value ("geometry").toByteArray ();
  if (geometry.size ())
    {
      restoreGeometry (geometry);
    }
  else
    {
      resize (1000, 400);
    }
}

void WideGraph::setRxFreq(int n)                                           //setRxFreq
{
  ui->widePlot->setRxFreq(n);
  if(m_mode!="Q65") ui->widePlot->draw(m_swide,false,false);
}

int WideGraph::rxFreq()                                                   //rxFreq
{
  return ui->widePlot->rxFreq();
}

int WideGraph::nStartFreq()                                             //nStartFreq
{
  return ui->widePlot->startFreq();
}

void WideGraph::wideFreezeDecode(int n)                              //wideFreezeDecode
{
  emit freezeDecode2(n);
}

void WideGraph::setRxRange ()
{
  ui->widePlot->setRxRange (Fmin ());
  ui->widePlot->DrawOverlay();
  ui->widePlot->update();
}

int WideGraph::Fmin()                                              //Fmin
{
  return "60m" == m_rxBand ? 0 : m_fMinPerBand.value (m_rxBand, 2500).toUInt ();
}

int WideGraph::Fmax()                                              //Fmax
{
  return std::min(5000,ui->widePlot->Fmax());
}

int WideGraph::fSpan()
{
  return ui->widePlot->fSpan ();
}

void WideGraph::setPeriod(double trperiod, int nsps)                  //SetPeriod
{
  m_TRperiod=trperiod;
  m_nsps=nsps;
  ui->widePlot->setNsps(trperiod, nsps);
  if (m_fitBand && m_fitBand->isChecked ()) m_fitTimer->start ();
}

void WideGraph::setTxFreq(int n)                                   //setTxFreq
{
  emit setXIT2(n);
  ui->widePlot->setTxFreq(n);
}

void WideGraph::setMode(QString mode)                              //setMode
{
  m_mode=mode;
  ui->fSplitSpinBox->setVisible(m_mode.startsWith("FST4"));
  ui->fSplitSpinBox->setEnabled(m_mode.startsWith("FST4"));
  ui->labTime->setVisible(!m_mode.startsWith("FST4"));
  ui->timestampComboBox->setVisible(!m_mode.startsWith("FST4"));
  ui->widePlot->setMode(mode);
  ui->widePlot->DrawOverlay();
  ui->widePlot->update();
}

void WideGraph::setSubMode(int n)                                  //setSubMode
{
  m_nSubMode=n;
  ui->widePlot->setSubMode(n);
  ui->widePlot->DrawOverlay();
  ui->widePlot->update();
}

void WideGraph::on_spec2dComboBox_currentIndexChanged(int index)
{
  ui->widePlot->setCurrent(false);
  ui->widePlot->setCumulative(false);
  ui->widePlot->setLinearAvg(false);
  ui->widePlot->setReference(false);
  ui->widePlot->setQ65_Sync(false);
  ui->widePlot->setTotalPower(false);
  ui->smoSpinBox->setEnabled(false);
  switch (index)
    {
    case 0:                     // Current
      ui->widePlot->setCurrent(true);
      break;
    case 1:                     // Cumulative
      ui->widePlot->setCumulative(true);
      break;
    case 2:                     // Linear Avg
      ui->widePlot->setLinearAvg(true);
      ui->smoSpinBox->setEnabled(true);
      break;
    case 3:                     // Reference
      ui->widePlot->setReference(true);
      break;
    case 4:
      ui->widePlot->setQ65_Sync(true);
      break;
    case 5:
      ui->widePlot->setTotalPower(true);
      break;
    }
  replot();
}

void WideGraph::on_fSplitSpinBox_valueChanged(int n)              //fSplit
{
  if (m_rxBand != "60m") m_fMinPerBand[m_rxBand] = n;
  setRxRange ();
}

void WideGraph::setFreq2(int rxFreq, int txFreq)                  //setFreq2
{
  emit setFreq3(rxFreq,txFreq);
}

void WideGraph::setDialFreq(double d)                             //setDialFreq
{
  ui->widePlot->setDialFreq(d);
}

void WideGraph::setRxBand (QString const& band)
{
  m_rxBand = band;
  if ("60m" == m_rxBand)
    {
      ui->fSplitSpinBox->setEnabled (false);
      ui->fSplitSpinBox->setValue (0);
    }
  else
    {
      ui->fSplitSpinBox->setValue (m_fMinPerBand.value (band, 2500).toUInt ());
      ui->fSplitSpinBox->setEnabled (m_mode.startsWith("FST4"));
    }
  ui->widePlot->setRxBand(band);
  setRxRange ();
}


void WideGraph::on_fStartSpinBox_valueChanged(int n)             //fStart
{
  ui->widePlot->setStartFreq(n);
}

void WideGraph::readPalette ()                                   //readPalette
{
  try
    {
      if (m_user_defined == m_waterfallPalette)
        {
          ui->widePlot->setColours (WFPalette {m_userPalette}.interpolate ());
        }
      else
        {
          ui->widePlot->setColours (WFPalette {m_palettes_path.absoluteFilePath (m_waterfallPalette + ".pal")}.interpolate());
        }
    }
  catch (std::exception const& e)
    {
      MessageBox::warning_message (this, tr ("Read Palette"), e.what ());
    }
}

void WideGraph::on_paletteComboBox_activated (QString const& palette)    //palette selector
{
  m_waterfallPalette = palette;
  readPalette();
  replot();
}

void WideGraph::on_cbFlatten_toggled(bool b)                          //Flatten On/Off
{
  m_bFlatten=b;
  if(m_bRef and m_bFlatten) {
    m_bRef=false;
    ui->cbRef->setChecked(false);
  }
  ui->widePlot->setFlatten(m_bFlatten,m_bRef);
}

void WideGraph::on_cbRef_toggled(bool b)
{
  m_bRef=b;
  if(m_bRef and m_bFlatten) {
    m_bFlatten=false;
    ui->cbFlatten->setChecked(false);
  }
  ui->widePlot->setFlatten(m_bFlatten,m_bRef);
}

void WideGraph::on_cbControls_toggled(bool b)
{
  m_controlsScroll->setVisible(b);
}

void WideGraph::on_timestampComboBox_currentIndexChanged(int n)
{
  m_timestamp = n;
  ui->widePlot->setTimestamp(n);
}

void WideGraph::on_cbBars_toggled(bool b)
{
  m_bars = b;
  ui->widePlot->setBars(m_bars);
}

void WideGraph::on_cbFreq_toggled(bool b)
{
  m_freq = b;
  ui->widePlot->showFreq(m_freq);
}

void WideGraph::on_adjust_palette_push_button_clicked (bool)   //Adjust Palette
{
  try
    {
      if (m_userPalette.design ())
        {
          m_waterfallPalette = m_user_defined;
          ui->paletteComboBox->setCurrentText (m_waterfallPalette);
          readPalette ();
        }
    }
  catch (std::exception const& e)
    {
      MessageBox::warning_message (this, tr ("Read Palette"), e.what ());
    }
}

bool WideGraph::flatten()                                              //Flatten
{
  return m_bFlatten;
}

bool WideGraph::useRef()                                              //Flatten
{
  return m_bRef;
}

void WideGraph::setReferenceSpectrumAvailable(bool available)
{
  if(!available) {
    m_bRef=false;
    ui->cbRef->setChecked(false);
    ui->widePlot->setFlatten(m_bFlatten,m_bRef);
  }
  ui->cbRef->setEnabled(available);
}

void WideGraph::clearReferenceSpectrum(bool reference_spectrum_available)
{
  m_bRef=false;
  ui->cbRef->setChecked(false);
  ui->widePlot->setFlatten(m_bFlatten,m_bRef);
  setReferenceSpectrumAvailable(reference_spectrum_available);
  m_n=0;
}

void WideGraph::replot()
{
  if(ui->widePlot->scaleOK()) ui->widePlot->replot();
}

void WideGraph::on_gainSlider_valueChanged(int value)                 //Gain
{
  ui->widePlot->setPlotGain(value);
  replot();
}

void WideGraph::on_zeroSlider_valueChanged(int value)                 //Zero
{
  ui->widePlot->setPlotZero(value);
  replot();
}

void WideGraph::on_gain2dSlider_valueChanged(int value)               //Gain2
{
  ui->widePlot->setPlot2dGain(value);
  if(ui->widePlot->TotalPower()) return;
  if(ui->widePlot->scaleOK ()) {
    ui->widePlot->draw(m_swide,false,false);
    if(m_mode=="Q65") ui->widePlot->draw(m_swide,false,true);
  }
}

void WideGraph::on_zero2dSlider_valueChanged(int value)               //Zero2
{
  ui->widePlot->setPlot2dZero(value);
  if(ui->widePlot->TotalPower()) return;
  if(ui->widePlot->scaleOK ()) {
    ui->widePlot->draw(m_swide,false,false);
    if(m_mode=="Q65") ui->widePlot->draw(m_swide,false,true);
  }
}

void WideGraph::setSuperFox(bool b)
{
  ui->widePlot->setSuperFox(b);
}

void WideGraph::setSuperHound(bool b)
{
  ui->widePlot->setSuperHound(b);
}

void WideGraph::setTol(int n)                                         //setTol
{
  ui->widePlot->setTol(n);
  ui->widePlot->DrawOverlay();
  ui->widePlot->update();
}

void WideGraph::setFST4_FreqRange(int fLow,int fHigh)
{
  ui->widePlot->setFST4_FreqRange(fLow,fHigh);
}

void WideGraph::setSingleDecode(bool b)
{
  ui->widePlot->setSingleDecode(b);
}

void WideGraph::on_smoSpinBox_valueChanged(int n)
{
  m_nsmo=n;
}

int WideGraph::smoothYellow()
{
  return m_nsmo;
}

void WideGraph::setWSPRtransmitted()
{
  m_bHaveTransmitted=true;
}

void WideGraph::setVHF(bool bVHF)
{
  ui->widePlot->setVHF(bVHF);
}

void WideGraph::on_sbPercent2dPlot_valueChanged(int n)
{
  m_Percent2DScreen=n;
  ui->widePlot->SetPercent2DScreen(n);
}

void WideGraph::setRedFile(QString fRed)
{
  ui->widePlot->setRedFile(fRed);
}

void WideGraph::setDiskUTC(int nutc)
{
  ui->widePlot->setDiskUTC(nutc);
}

void WideGraph::setDarkStyle(bool b)
{
  ui->widePlot->setDarkStyle(b);
}

void WideGraph::restartTotalPower()
{
  ui->widePlot->restartTotalPower();
}

void WideGraph::resizeEvent (QResizeEvent * e)
{
  QDialog::resizeEvent (e);
  if (m_controlsScroll)
    {
      // tall enough for the controls, plus the scroll bar when it shows
      // when the window is too short for them, they scroll up and down
      // too and the waterfall keeps some room
      int const bar = m_controlsScroll->horizontalScrollBar ()->sizeHint ().height ();
      int const room = qMax (40, height () - 110);
      int h = ui->controls_widget->minimumSizeHint ().height ();
      bool const too_short = h > room;
      int const width_left = m_controlsScroll->width () - (too_short ? m_controlsScroll->verticalScrollBar ()->sizeHint ().width () : 0);
      if (ui->controls_widget->minimumSizeHint ().width () > width_left) h += bar;
      m_controlsScroll->setVerticalScrollBarPolicy (too_short ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
      m_controlsScroll->setFixedHeight (qMin (h, room));
    }
  if (m_fitBand && m_fitBand->isChecked ()) m_fitTimer->start ();
}

void WideGraph::fit_band ()
{
  if (!m_fitBand || !m_fitBand->isChecked ()) return;
  // FT8 and FT4 signals sit between 200 and about 3500 Hz above the dial
  double const span = 3500.0;
  int const w = ui->widePlot->width ();
  double const bin = ui->widePlot->fftBinWidth ();
  if (w < 50 || bin <= 0.) return;
  int n = static_cast<int> (std::ceil (span / (w * bin)));
  n = qBound (ui->bppSpinBox->minimum (), n, ui->bppSpinBox->maximum ());
  if (n != ui->bppSpinBox->value ()) ui->bppSpinBox->setValue (n);
}
