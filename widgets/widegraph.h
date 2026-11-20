// -*- Mode: C++ -*-
#ifndef WIDEGRAPH_H_
#define WIDEGRAPH_H_

#include <QDialog>
#include <QScopedPointer>
#include <QDir>
#include <QHash>
#include <QVariant>
#include "WFPalette.hpp"

#define MAX_SCREENSIZE 2048

namespace Ui {
  class WideGraph;
}

class QSettings;
class Configuration;

class QScrollArea;
class QCheckBox;
class QTimer;
class QResizeEvent;

class WideGraph : public QDialog
{
  Q_OBJECT

public:
  explicit WideGraph(QSettings *, QWidget *parent = 0);
  ~WideGraph ();

  void   dataSink2(float s[], float df3, int ihsym, int ndiskdata, float pdB);
  void   setRxFreq(int n);
  int    rxFreq();
  int    nStartFreq();
  int    Fmin();
  int    Fmax();
  int    fSpan();
  void   saveSettings();
  void   setFsample(int n);
  void   setPeriod(double trperiod, int nsps);
  void   setTxFreq(int n);
  void   setMode(QString mode);
  void   setSubMode(int n);
  bool   flatten();
  bool   useRef();
  void   setReferenceSpectrumAvailable(bool available);
  void   clearReferenceSpectrum(bool reference_spectrum_available);
  void   setTol(int n);
  void   setSuperFox(bool b);
  void   setSuperHound(bool b);
  int    smoothYellow();
  void   setRxBand (QString const& band);
  void   setWSPRtransmitted();
  void   drawRed(int ia, int ib);
  void   setVHF(bool bVHF);
  void   setRedFile(QString fRed);
  void   setFST4_FreqRange(int fLow,int fHigh);
  void   setSingleDecode(bool b);
  void   setDiskUTC(int nutc);
  void   restartTotalPower();
  void	 setDarkStyle(bool b);
  void   restoreWindowGeometry ();   // used when the waterfall is detached from the main window

  // a decoded station's call shown on its signal in the waterfall
  void addDecodeLabel (int freq, QString const& call, int snr, QColor const& colour, QString const& tip, QString const& period);
  void clearDecodeLabels ();

signals:
  void labelDoubleClicked (QString const& call, Qt::KeyboardModifiers);
  void freezeDecode2(int n);
  void f11f12(int n);
  void setXIT2(int n);
  void setFreq3(int rxFreq, int txFreq);

public slots:
  void wideFreezeDecode(int n);
  void setFreq2(int rxFreq, int txFreq);
  void setDialFreq(double d);

protected:
  void keyPressEvent (QKeyEvent *e) override;
  void resizeEvent (QResizeEvent *) override;
  void closeEvent (QCloseEvent *) override;

private slots:
  void on_waterfallAvgSpinBox_valueChanged(int arg1);
  void on_bppSpinBox_valueChanged(int arg1);
  void on_spec2dComboBox_currentIndexChanged(int);
  void on_fSplitSpinBox_valueChanged(int n);
  void on_fStartSpinBox_valueChanged(int n);
  void on_paletteComboBox_activated(const QString &palette);
  void on_timestampComboBox_currentIndexChanged(int n);
  void on_cbFlatten_toggled(bool b);
  void on_cbRef_toggled(bool b);
  void on_cbControls_toggled(bool b);
  void on_cbBars_toggled(bool b);
  void on_cbFreq_toggled(bool b);
  void on_adjust_palette_push_button_clicked (bool);
  void on_gainSlider_valueChanged(int value);
  void on_zeroSlider_valueChanged(int value);
  void on_gain2dSlider_valueChanged(int value);
  void on_zero2dSlider_valueChanged(int value);
  void on_smoSpinBox_valueChanged(int n);  
  void on_sbPercent2dPlot_valueChanged(int n);

private:
  void readPalette ();
  void fit_band ();             // pick Bins/Pixel so the whole band fits the width
  void setRxRange ();
  void replot();

  QScopedPointer<Ui::WideGraph> ui;
  QScrollArea * m_controlsScroll {nullptr};
  class QComboBox * m_displayStyle;
  QCheckBox * m_periodLines;
  QCheckBox * m_fitBand {nullptr};
  // the toolbar over the waterfall
  void build_toolbar ();
  void sync_toolbar ();
  QWidget * m_toolbar {nullptr};
  class QButtonGroup * m_viewGroup {nullptr};
  class QButtonGroup * m_paletteGroup {nullptr};
  class QSlider * m_tbGain {nullptr};
  class QSlider * m_tbFloor {nullptr};
  class QSlider * m_tbSpeed {nullptr};
  class QPushButton * m_tbNames {nullptr};
  class QPushButton * m_tbMore {nullptr};
  class QPushButton * m_tbPill {nullptr};
  QTimer * m_fitTimer {nullptr};

  QSettings * m_settings;
  QDir m_palettes_path;
  WFPalette m_userPalette;
  QHash<QString, QVariant> m_fMinPerBand;

  double m_tr0;
  double m_TRperiod;

  qint32 m_waterfallAvg;
  qint32 m_nsps;
  qint32 m_fMax;
  qint32 m_nSubMode;
  qint32 m_nsmo;
  qint32 m_Percent2DScreen;
  qint32 m_jz=MAX_SCREENSIZE;
  qint32 m_n;
  qint32 m_timestamp;

  bool	 m_bars;
  bool	 m_freq;
  bool   m_bFlatten;
  bool   m_bRef;
  bool   m_bHaveTransmitted;    //Set true at end of a WSPR or FT4 transmission

  QString m_rxBand;
  QString m_mode;
  QString m_waterfallPalette;  
  float   m_swide[MAX_SCREENSIZE];
  QString m_user_defined;
};

#endif // WIDEGRAPH_H
