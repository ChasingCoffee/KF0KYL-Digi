#include "Win11Theme.hpp"

#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>
#include <QEvent>
#include <QWidget>
#include <QHash>

#ifdef Q_OS_WIN
#include <QLibrary>
#include <windows.h>
#endif

#include "qt_helpers.hpp"

#include "moc_Win11Theme.cpp"

bool Win11Theme::dark_ {false};

namespace
{
  // Colour tokens loosely following the Windows 11 Fluent design kit
  struct Tokens
  {
    char const * bg;            // window background (Mica base)
    char const * layer;         // slightly raised layer (status bar, headers)
    char const * card;          // card / content surfaces
    char const * ctrl;          // control face
    char const * ctrl_hover;
    char const * ctrl_press;
    char const * border;        // subtle stroke
    char const * border_strong; // control bottom stroke
    char const * text;
    char const * text_secondary;
    char const * text_disabled;
    char const * accent;
    char const * accent_hover;
    char const * accent_press;
    char const * accent_text;   // text on accent fill
    char const * input;         // text box face
    char const * scroll_thumb;
    char const * selection;
  };

  Tokens const dark_tokens {
    "#202020", "#1C1C1C", "#2B2B2B", "#2D2D2D", "#373737", "#272727",
    "#3A3A3A", "#4D4D4D", "#FFFFFF", "#C5C5C5", "#7A7A7A",
    "#60CDFF", "#5AB9E6", "#4FA6CE", "#000000",
    "#1F1F1F", "#6E6E6E", "#0F5A84"
  };

  Tokens const light_tokens {
    "#F3F3F3", "#EBEBEB", "#FFFFFF", "#FBFBFB", "#F5F5F5", "#EDEDED",
    "#E1E1E1", "#C6C6C6", "#1B1B1B", "#5C5C5C", "#A3A3A3",
    "#005FB8", "#1A70C4", "#2F7FCB", "#FFFFFF",
    "#FFFFFF", "#9A9A9A", "#CDE4F7"
  };

  // more colour schemes, same token order as above
  Tokens const midnight_tokens {
    "#0F1626", "#0B111E", "#16203A", "#1B2742", "#24335A", "#18223B",
    "#2A3A5E", "#3B4F7A", "#E8EEFF", "#A9B6D6", "#5B6A8C",
    "#4FC3F7", "#47B0DE", "#3E9CC4", "#000000",
    "#121B30", "#3E5078", "#1E4E7A"
  };

  // for low vision: pure black, white text, bright yellow accents
  Tokens const contrast_tokens {
    "#000000", "#000000", "#0A0A0A", "#000000", "#262626", "#3A3A3A",
    "#FFFFFF", "#FFFFFF", "#FFFFFF", "#FFFF00", "#A0A0A0",
    "#FFFF00", "#FFEA00", "#E6D200", "#000000",
    "#000000", "#FFFFFF", "#0000C8"
  };

  // dark red on black keeps your eyes adjusted for night operating
  Tokens const night_red_tokens {
    "#120404", "#0E0303", "#1C0707", "#220909", "#2E0C0C", "#1A0606",
    "#3A1010", "#5A1A1A", "#FF6B6B", "#C04848", "#6A2626",
    "#FF3B3B", "#E63535", "#CC2F2F", "#000000",
    "#160505", "#5A1A1A", "#5A1414"
  };

  // the green of the KF0KYL Digi logo
  Tokens const green_tokens {
    "#0B0F0B", "#080B08", "#121912", "#152015", "#1E2E1E", "#101810",
    "#243524", "#355235", "#EAF7E6", "#A8C6A0", "#5D7358",
    "#7CDB3A", "#6FC734", "#62B12E", "#000000",
    "#0E140E", "#3D5C3A", "#2E5A1E"
  };

  // a soft paper-coloured light scheme, easier on the eyes than white
  Tokens const paper_tokens {
    "#F4ECD8", "#EDE3CB", "#FBF6EA", "#F7F0DF", "#EFE5CE", "#E6DABE",
    "#D9CBA8", "#BBA77A", "#3B2F1E", "#6B5A40", "#A8977A",
    "#9A5B13", "#A86A22", "#B37A33", "#FFFFFF",
    "#FFFBF2", "#BBA77A", "#EBD9AE"
  };

  struct SchemeInfo {char const * name; Tokens const * tokens; bool dark;};
  SchemeInfo const schemes[] = {
    {QT_TRANSLATE_NOOP ("Win11Theme", "Light"), &light_tokens, false},
    {QT_TRANSLATE_NOOP ("Win11Theme", "Dark"), &dark_tokens, true},
    {QT_TRANSLATE_NOOP ("Win11Theme", "Midnight Blue"), &midnight_tokens, true},
    {QT_TRANSLATE_NOOP ("Win11Theme", "High Contrast"), &contrast_tokens, true},
    {QT_TRANSLATE_NOOP ("Win11Theme", "Night Red"), &night_red_tokens, true},
    {QT_TRANSLATE_NOOP ("Win11Theme", "Digi Green"), &green_tokens, true},
    {QT_TRANSLATE_NOOP ("Win11Theme", "Paper"), &paper_tokens, false},
  };
  int const scheme_count = sizeof schemes / sizeof schemes[0];

  int current_scheme {0};
  Tokens const& current_tokens () {return *schemes[current_scheme].tokens;}

  // Parts of the main window set their own colours with fixed values
  // written for the plain Light and Dark schemes. In the other schemes
  // those values are swapped for the scheme's own as they are applied.
  QString remap_sheet (QString sheet)
  {
    if (current_scheme <= 1) return sheet;
    auto const& t = current_tokens ();
    QList<QPair<QString, QString>> const map {
      {"#3A3A3A", t.ctrl}, {"#4D4D4D", t.border_strong}, {"#F0F0F0", t.text},
      {"#1F1F1F", t.input}, {"#2D2D2D", t.ctrl}, {"#373737", t.ctrl_hover},
      {"#202020", t.bg}, {"#2B2B2B", t.card},
      {"#e1e1e1", t.ctrl}, {"#adadad", t.border_strong},
    };
    for (auto const& p : map) sheet.replace (p.first, p.second, Qt::CaseInsensitive);
    return sheet;
  }

  // %1 style tokens are replaced by name, see fill() below
  char const * const qss_template = R"QSS(
QWidget {
  background-color: @bg;
  color: @text;
  selection-background-color: @selection;
  selection-color: @text;
}
QWidget:disabled { color: @text_disabled; }
QLabel, QCheckBox, QRadioButton { background: transparent; }

QMainWindow::separator { background: @bg; width: 6px; height: 6px; }

/* ---- menus ---- */
QMenuBar { background: @bg; padding: 2px 4px; }
QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: 5px; }
QMenuBar::item:selected { background: @ctrl_hover; }
QMenuBar::item:pressed { background: @ctrl_press; }
QMenu {
  background: @card;
  border: 1px solid @border;
  border-radius: 8px;
  padding: 4px;
}
QMenu::item { padding: 6px 28px 6px 24px; border-radius: 4px; background: transparent; }
QMenu::item:selected { background: @ctrl_hover; }
QMenu::item:disabled { color: @text_disabled; }
QMenu::separator { height: 1px; background: @border; margin: 4px 6px; }

/* ---- buttons ---- */
QPushButton, QToolButton {
  background-color: @ctrl;
  color: @text;
  border: 1px solid @border;
  border-bottom-color: @border_strong;
  border-radius: 5px;
  padding: 4px 6px;
  min-height: 18px;
}
QPushButton:hover, QToolButton:hover { background-color: @ctrl_hover; }
QPushButton:pressed, QToolButton:pressed {
  background-color: @ctrl_press;
  color: @text_secondary;
  border-bottom-color: @border;
}
QPushButton:checked, QToolButton:checked {
  background-color: @accent;
  color: @accent_text;
  border: 1px solid @accent_press;
}
QPushButton:checked:hover, QToolButton:checked:hover { background-color: @accent_hover; }
QPushButton:disabled, QToolButton:disabled {
  background-color: @ctrl_press;
  color: @text_disabled;
  border-color: @border;
}
QPushButton:default { border: 1px solid @accent; }
QToolButton { padding: 3px 6px; }

/* ---- text inputs ---- */
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QDateTimeEdit, QDateEdit, QTimeEdit {
  background-color: @input;
  color: @text;
  border: 1px solid @border;
  border-bottom: 1px solid @border_strong;
  border-radius: 4px;
  padding: 2px 4px;
  min-height: 20px;
}
QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover,
QDateTimeEdit:hover { background-color: @ctrl_hover; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus,
QDateTimeEdit:focus {
  background-color: @input;
  border-bottom: 2px solid @accent;
}
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled {
  color: @text_disabled;
  background-color: @ctrl_press;
}
QComboBox QAbstractItemView {
  background: @card;
  border: 1px solid @border;
  border-radius: 6px;
  padding: 4px;
  outline: 0;
  selection-background-color: @ctrl_hover;
  selection-color: @text;
}

/* spin box and combo box arrows */
QSpinBox, QDoubleSpinBox, QDateTimeEdit, QDateEdit, QTimeEdit { padding-right: 20px; }
QAbstractSpinBox::up-button {
  subcontrol-origin: border; subcontrol-position: top right;
  width: 18px; border: none; background: transparent;
  margin: 2px 2px 0 0; border-radius: 3px;
}
QAbstractSpinBox::down-button {
  subcontrol-origin: border; subcontrol-position: bottom right;
  width: 18px; border: none; background: transparent;
  margin: 0 2px 2px 0; border-radius: 3px;
}
QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover { background: @ctrl_hover; }
QAbstractSpinBox::up-arrow { image: url(:/win11/up_@theme.png); width: 10px; height: 10px; }
QAbstractSpinBox::down-arrow { image: url(:/win11/down_@theme.png); width: 10px; height: 10px; }
QAbstractSpinBox::up-arrow:disabled, QAbstractSpinBox::up-arrow:off,
QAbstractSpinBox::down-arrow:disabled, QAbstractSpinBox::down-arrow:off { image: none; }
QComboBox { padding-right: 16px; }
QComboBox::drop-down {
  subcontrol-origin: padding; subcontrol-position: top right;
  width: 16px; border: none; background: transparent;
}
QComboBox::down-arrow { image: url(:/win11/down_@theme.png); width: 10px; height: 10px; }

/* ---- check boxes and radio buttons ---- */
QCheckBox, QRadioButton { spacing: 6px; }
QCheckBox::indicator {
  width: 16px; height: 16px;
  border: 1px solid @border_strong; border-radius: 4px;
  background: @input;
}
QCheckBox::indicator:hover { background: @ctrl_hover; }
QCheckBox::indicator:checked, QCheckBox::indicator:indeterminate {
  background: @accent; border-color: @accent;
  image: url(:/win11/check_@theme.png);
}
QCheckBox::indicator:checked:hover { background: @accent_hover; border-color: @accent_hover; }
QCheckBox::indicator:disabled { background: @ctrl_press; border-color: @border; }
QCheckBox::indicator:checked:disabled { background: @text_disabled; border-color: @text_disabled; }
QRadioButton::indicator {
  width: 16px; height: 16px;
  border: 1px solid @border_strong; border-radius: 9px;
  background: @input;
}
QRadioButton::indicator:hover { background: @ctrl_hover; }
QRadioButton::indicator:checked {
  width: 8px; height: 8px;
  border: 5px solid @accent; border-radius: 9px;
  background: @accent_text;
}
QRadioButton::indicator:disabled { border-color: @border; }

/* ---- text areas, lists, tables ---- */
QTextEdit, QPlainTextEdit, QTextBrowser, QListView, QListWidget, QTreeView,
QTableView, QTableWidget {
  background-color: @card;
  color: @text;
  border: 1px solid @border;
  border-radius: 8px;
  padding: 2px;
}
QHeaderView::section {
  background-color: @layer;
  color: @text_secondary;
  border: none;
  border-bottom: 1px solid @border;
  border-right: 1px solid @border;
  padding: 4px 6px;
}
QTableCornerButton::section { background: @layer; border: none; }

/* ---- tabs ---- */
QTabWidget::pane {
  background: @card;
  border: 1px solid @border;
  border-radius: 8px;
  top: -1px;
}
QTabBar { background: transparent; }
QTabBar::tab {
  background: transparent;
  color: @text_secondary;
  padding: 6px 14px;
  margin-right: 2px;
  border: none;
  border-bottom: 2px solid transparent;
}
QTabBar::tab:hover { color: @text; background: @ctrl_hover; border-top-left-radius: 5px; border-top-right-radius: 5px; }
QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent; }

/* ---- grouping ---- */
QGroupBox {
  background-color: @card;
  border: 1px solid @border;
  border-radius: 8px;
  margin-top: 16px;
  padding: 10px 6px 6px 6px;
}
QGroupBox::title {
  subcontrol-origin: margin;
  subcontrol-position: top left;
  left: 10px;
  padding: 0 4px;
  color: @text_secondary;
  background: transparent;
}
QFrame[frameShape="4"], QFrame[frameShape="5"] { color: @border; }  /* HLine / VLine */

/* ---- scroll bars (thin, Windows 11 style) ---- */
QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
QScrollBar::handle:vertical { background: @scroll_thumb; min-height: 28px; border-radius: 4px; margin: 0 2px; }
QScrollBar::handle:horizontal { background: @scroll_thumb; min-width: 28px; border-radius: 4px; margin: 2px 0; }
QScrollBar::handle:hover { background: @text_secondary; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

/* ---- sliders ---- */
QSlider::groove:horizontal { height: 4px; background: @border_strong; border-radius: 2px; }
QSlider::groove:vertical { width: 4px; background: @border_strong; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::add-page:vertical { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal {
  background: @accent; border: 4px solid @ctrl;
  width: 10px; height: 10px; margin: -7px 0; border-radius: 9px;
}
QSlider::handle:vertical {
  background: @accent; border: 4px solid @ctrl;
  width: 10px; height: 10px; margin: 0 -7px; border-radius: 9px;
}
QSlider::handle:hover { border-width: 3px; }

/* ---- progress ---- */
QProgressBar {
  background-color: @border;
  color: @text;
  border: none;
  border-radius: 4px;
  text-align: center;
  min-height: 14px;
}
QProgressBar::chunk { background-color: @accent; border-radius: 4px; }

/* ---- splitters ---- */
QSplitter::handle { background: transparent; border-radius: 3px; }
QSplitter::handle:vertical { height: 8px; image: url(:/win11/grip_h_@theme.png); }
QSplitter::handle:horizontal { width: 8px; image: url(:/win11/grip_v_@theme.png); }
QSplitter::handle:hover { background: @border_strong; }
QSplitter::handle:pressed { background: @accent; }

/* ---- status bar & tooltips ---- */
QStatusBar { background: @layer; border-top: 1px solid @border; }
QStatusBar::item { border: none; }
QStatusBar QLabel { padding: 0 6px; }
QToolTip {
  background-color: @card;
  color: @text;
  border: 1px solid @border;
  border-radius: 4px;
  padding: 4px 6px;
}

/* ---- dialogs ---- */
QDialog { background-color: @bg; }
QDialogButtonBox QPushButton { min-width: 72px; }
)QSS";

  QString fill (Tokens const& t, QString const& theme)
  {
    QString s {qss_template};
    s.replace ("@theme", theme);
    // longest names first so that e.g. @accent_text is not clobbered by @accent
    QList<QPair<QString, QString>> const map {
      {"@border_strong", t.border_strong},
      {"@text_secondary", t.text_secondary},
      {"@text_disabled", t.text_disabled},
      {"@accent_hover", t.accent_hover},
      {"@accent_press", t.accent_press},
      {"@accent_text", t.accent_text},
      {"@scroll_thumb", t.scroll_thumb},
      {"@ctrl_hover", t.ctrl_hover},
      {"@ctrl_press", t.ctrl_press},
      {"@selection", t.selection},
      {"@accent", t.accent},
      {"@border", t.border},
      {"@layer", t.layer},
      {"@input", t.input},
      {"@card", t.card},
      {"@ctrl", t.ctrl},
      {"@text", t.text},
      {"@bg", t.bg},
    };
    for (auto const& p : map)
      {
        s.replace (p.first, p.second);
      }
    return s;
  }

  QPalette make_palette (Tokens const& t)
  {
    QPalette p;
    QColor const text {t.text};
    QColor const disabled {t.text_disabled};
    p.setColor (QPalette::Window, QColor {t.bg});
    p.setColor (QPalette::WindowText, text);
    p.setColor (QPalette::Base, QColor {t.input});
    p.setColor (QPalette::AlternateBase, QColor {t.card});
    p.setColor (QPalette::ToolTipBase, QColor {t.card});
    p.setColor (QPalette::ToolTipText, text);
    p.setColor (QPalette::Text, text);
    p.setColor (QPalette::Button, QColor {t.ctrl});
    p.setColor (QPalette::ButtonText, text);
    p.setColor (QPalette::BrightText, Qt::red);
    p.setColor (QPalette::Link, QColor {t.accent});
    p.setColor (QPalette::Highlight, QColor {t.accent});
    p.setColor (QPalette::HighlightedText, QColor {t.accent_text});
    p.setColor (QPalette::Light, QColor {t.ctrl_hover});
    p.setColor (QPalette::Midlight, QColor {t.border});
    p.setColor (QPalette::Mid, QColor {t.border_strong});
    p.setColor (QPalette::Dark, QColor {t.layer});
    p.setColor (QPalette::Shadow, QColor {"#000000"});
#if QT_VERSION >= QT_VERSION_CHECK (5, 12, 0)
    p.setColor (QPalette::PlaceholderText, QColor {t.text_secondary});
#endif
    p.setColor (QPalette::Disabled, QPalette::WindowText, disabled);
    p.setColor (QPalette::Disabled, QPalette::Text, disabled);
    p.setColor (QPalette::Disabled, QPalette::ButtonText, disabled);
    return p;
  }

#ifdef Q_OS_WIN
  using DwmSetWindowAttributeFn = HRESULT (WINAPI *) (HWND, DWORD, LPCVOID, DWORD);

  DwmSetWindowAttributeFn dwm_set_attribute ()
  {
    static DwmSetWindowAttributeFn fn {nullptr};
    static bool tried {false};
    if (!tried)
      {
        tried = true;
        fn = reinterpret_cast<DwmSetWindowAttributeFn> (QLibrary::resolve ("dwmapi", "DwmSetWindowAttribute"));
      }
    return fn;
  }
#endif
}

void Win11Theme::install (QApplication * app)
{
  // Fusion renders style sheets consistently and honours the palette
  if (auto * fusion = QStyleFactory::create ("Fusion"))
    {
      app->setStyle (fusion);
    }
  app->installEventFilter (new Win11Theme {app});
}

QString Win11Theme::styleSheet (bool dark)
{
  Q_UNUSED (dark);
  return fill (current_tokens (), dark_ ? "dark" : "light");
}

int Win11Theme::schemeCount () {return scheme_count;}
QString Win11Theme::schemeName (int i)
{
  return i >= 0 && i < scheme_count ? tr (schemes[i].name) : QString {};
}
int Win11Theme::scheme () {return current_scheme;}
bool Win11Theme::schemeIsDark (int i) {return i >= 0 && i < scheme_count && schemes[i].dark;}

void Win11Theme::apply (bool dark, QFont const& font, QString const& extra)
{
  // the old two-way switch: Light or Dark
  applyScheme (dark ? 1 : 0, font, extra);
}

void Win11Theme::applyScheme (int scheme, QFont const& font, QString const& extra)
{
  current_scheme = qBound (0, scheme, scheme_count - 1);
  dark_ = schemes[current_scheme].dark;
  qApp->setPalette (make_palette (current_tokens ()));
  qApp->setFont (font);
  qApp->setStyleSheet (styleSheet (dark_) + extra + "* {" + font_as_stylesheet (font) + '}');
  // re-colour widgets that set their own colours
  for (auto * w : qApp->allWidgets ())
    {
      auto const original = w->property ("w11_original").toString ();
      if (!original.isEmpty ())
        {
          auto const mapped = remap_sheet (original);
          w->setProperty ("w11_applied", mapped);
          w->setStyleSheet (mapped);
        }
    }
  for (auto * w : qApp->topLevelWidgets ())
    {
      updateTitleBar (w);
    }
}

void Win11Theme::updateTitleBar (QWidget * w)
{
#ifdef Q_OS_WIN
  if (!w || !w->isWindow () || !w->testAttribute (Qt::WA_WState_Created)) return;
  if (auto fn = dwm_set_attribute ())
    {
      auto hwnd = reinterpret_cast<HWND> (w->winId ());
      BOOL value = dark_ ? TRUE : FALSE;
      // DWMWA_USE_IMMERSIVE_DARK_MODE is 20 on Windows 10 20H1 and later, 19 before
      if (FAILED (fn (hwnd, 20, &value, sizeof value)))
        {
          fn (hwnd, 19, &value, sizeof value);
        }
      // DWMWA_WINDOW_CORNER_PREFERENCE (33) = DWMWCP_ROUND (2), Windows 11 only
      DWORD corner = 2;
      fn (hwnd, 33, &corner, sizeof corner);
      // make the non-client area repaint now rather than on next activation
      SetWindowPos (hwnd, nullptr, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
#else
  Q_UNUSED (w);
#endif
}

QString Win11Theme::controlBackground ()
{
  return current_tokens ().ctrl;
}

QString Win11Theme::controlBorder ()
{
  return current_tokens ().border_strong;
}

QString Win11Theme::inputBackground ()
{
  return current_tokens ().input;
}

bool Win11Theme::eventFilter (QObject * obj, QEvent * e)
{
  if (e->type () == QEvent::StyleChange && current_scheme > 1)
    {
      if (auto * w = qobject_cast<QWidget *> (obj))
        {
          auto const sheet = w->styleSheet ();
          if (!sheet.isEmpty () && sheet != w->property ("w11_applied").toString ())
            {
              // a new sheet from the program: keep the original, apply the remapped one
              w->setProperty ("w11_original", sheet);
              auto const mapped = remap_sheet (sheet);
              w->setProperty ("w11_applied", mapped);
              if (mapped != sheet) w->setStyleSheet (mapped);
            }
        }
    }
  else if (e->type () == QEvent::StyleChange)
    {
      if (auto * w = qobject_cast<QWidget *> (obj))
        {
          auto const sheet = w->styleSheet ();
          if (!sheet.isEmpty () && sheet != w->property ("w11_applied").toString ())
            {
              w->setProperty ("w11_original", sheet);
              w->setProperty ("w11_applied", sheet);
            }
        }
    }
#ifdef Q_OS_WIN
  if (e->type () == QEvent::Show || e->type () == QEvent::WinIdChange)
    {
      if (auto * w = qobject_cast<QWidget *> (obj))
        {
          if (w->isWindow ()) updateTitleBar (w);
        }
    }
#endif
  return false;
}
