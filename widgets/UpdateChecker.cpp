#include "UpdateChecker.hpp"

#include <QSettings>
#include <QWidget>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QLabel>
#include <QTextBrowser>
#include <QCheckBox>
#include <QPushButton>
#include <QProgressDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QDateTime>
#include <QRegularExpression>
#include <QApplication>
#include <QClipboard>

#include "revision_utils.hpp"

namespace
{
  QString const releases_api {"https://api.github.com/repos/ccoo26/KF0KYL-Digi/releases/latest"};
  QString const releases_page {"https://github.com/ccoo26/KF0KYL-Digi/releases/latest"};

  QList<int> numbers (QString const& v)
  {
    QList<int> out;
    auto it = QRegularExpression {"\\d+"}.globalMatch (v);
    while (it.hasNext () && out.size () < 4) out << it.next ().captured ().toInt ();
    while (out.size () < 3) out << 0;
    return out;
  }

  QString current_version ()
  {
    return version ();
  }

  // the file on the release page for this computer
  bool wanted_asset (QString const& name)
  {
    auto const n = name.toLower ();
#if defined (Q_OS_WIN)
    return n.endsWith (".exe") && n.contains ("win");
#elif defined (Q_OS_LINUX)
    bool const arm = QSysInfo::currentCpuArchitecture ().contains ("arm") || QSysInfo::currentCpuArchitecture ().contains ("aarch64");
    if (qEnvironmentVariableIsSet ("APPIMAGE")) return n.endsWith (".appimage") && n.contains (arm ? "aarch64" : "x86_64");
    return n.endsWith (".deb") && (arm ? n.contains ("aarch64") || n.contains ("arm64") : n.contains ("x86_64") || n.contains ("amd64"));
#else
    Q_UNUSED (n);
    return false;
#endif
  }
}

UpdateChecker::UpdateChecker (QSettings * settings, QWidget * window)
  : QObject {window}
  , settings_ {settings}
  , window_ {window}
  , net_ {new QNetworkAccessManager {this}}
{
}

bool UpdateChecker::newer (QString const& theirs, QString const& ours)
{
  auto const a = numbers (theirs), b = numbers (ours);
  for (int i = 0; i < qMin (a.size (), b.size ()); ++i)
    {
      if (a[i] != b[i]) return a[i] > b[i];
    }
  return false;
}

void UpdateChecker::check_at_start ()
{
  settings_->beginGroup ("Updates");
  bool const on = settings_->value ("CheckAtStart", true).toBool ();
  auto const last = settings_->value ("LastCheck").toDateTime ();
  settings_->endGroup ();
  if (!on) return;
  if (last.isValid () && last.secsTo (QDateTime::currentDateTimeUtc ()) < 20 * 3600) return;
  QTimer::singleShot (8000, this, [this] {check (false);});
}

void UpdateChecker::check_now ()
{
  check (true);
}

void UpdateChecker::check (bool manual)
{
  if (busy_) return;
  busy_ = true;
  QNetworkRequest req {QUrl {releases_api}};
  req.setHeader (QNetworkRequest::UserAgentHeader, "KF0KYL-Digi/" + current_version ());
  req.setRawHeader ("Accept", "application/vnd.github+json");
  auto * reply = net_->get (req);
  connect (reply, &QNetworkReply::finished, this, [this, reply, manual] {
      reply->deleteLater ();
      busy_ = false;
      if (reply->error ())
        {
          if (manual)
            {
              QMessageBox::warning (window_, tr ("Check for updates"),
                                    tr ("Couldn't reach GitHub to check for a new version:\n%1").arg (reply->errorString ()));
            }
          return;
        }
      settings_->setValue ("Updates/LastCheck", QDateTime::currentDateTimeUtc ());
      auto const release = QJsonDocument::fromJson (reply->readAll ()).object ();
      auto const tag = release.value ("tag_name").toString ();
      if (tag.isEmpty () || release.value ("draft").toBool () || release.value ("prerelease").toBool ())
        {
          if (manual) QMessageBox::information (window_, tr ("Check for updates"), tr ("No release was found on GitHub."));
          return;
        }
      if (!newer (tag, current_version ()))
        {
          if (manual)
            {
              QMessageBox::information (window_, tr ("Check for updates"),
                                        tr ("You have the latest version of KF0KYL Digi (%1).").arg (current_version ()));
            }
          return;
        }
      if (!manual && settings_->value ("Updates/Skip").toString () == tag) return;
      QString asset_name, asset_url;
      qint64 asset_size = 0;
      for (auto const a : release.value ("assets").toArray ())
        {
          auto const o = a.toObject ();
          if (wanted_asset (o.value ("name").toString ()))
            {
              asset_name = o.value ("name").toString ();
              asset_url = o.value ("browser_download_url").toString ();
              asset_size = qint64 (o.value ("size").toDouble ());
              break;
            }
        }
      offer (tag, release.value ("body").toString (), release.value ("html_url").toString (releases_page),
             asset_name, asset_url, asset_size);
    });
}

void UpdateChecker::offer (QString const& tag, QString const& notes, QString const& page,
                           QString const& asset_name, QString const& asset_url, qint64 asset_size)
{
  QDialog dlg {window_};
  dlg.setWindowTitle (tr ("KF0KYL Digi update"));
  dlg.setMinimumWidth (520);
  auto * layout = new QVBoxLayout {&dlg};
  auto * head = new QLabel {tr ("<h3>KF0KYL Digi %1 is available</h3><p>You have version %2.</p>")
                            .arg (QString {tag}.remove (QRegularExpression {"^[vV]"}), current_version ()), &dlg};
  layout->addWidget (head);
  auto * text = new QTextBrowser {&dlg};
  text->setOpenExternalLinks (true);
#if QT_VERSION >= QT_VERSION_CHECK (5, 14, 0)
  text->setMarkdown (notes.isEmpty () ? tr ("*No release notes.*") : notes);
#else
  text->setPlainText (notes);
#endif
  text->setMinimumHeight (220);
  layout->addWidget (text);
  auto * at_start = new QCheckBox {tr ("Check for updates when KF0KYL Digi starts"), &dlg};
  at_start->setChecked (settings_->value ("Updates/CheckAtStart", true).toBool ());
  layout->addWidget (at_start);
  auto * buttons = new QDialogButtonBox {&dlg};
  QPushButton * install = nullptr;
  if (!asset_url.isEmpty ())
    {
#if defined (Q_OS_WIN)
      install = buttons->addButton (tr ("Download and install"), QDialogButtonBox::AcceptRole);
#else
      install = buttons->addButton (tr ("Download"), QDialogButtonBox::AcceptRole);
#endif
      install->setDefault (true);
    }
  auto * open_page = buttons->addButton (tr ("Open release page"), QDialogButtonBox::ActionRole);
  auto * later = buttons->addButton (tr ("Remind me later"), QDialogButtonBox::RejectRole);
  auto * skip = buttons->addButton (tr ("Skip this version"), QDialogButtonBox::DestructiveRole);
  layout->addWidget (buttons);
  QAbstractButton * chosen = nullptr;
  connect (buttons, &QDialogButtonBox::clicked, &dlg, [&] (QAbstractButton * b) {chosen = b; dlg.done (0);});
  dlg.exec ();
  settings_->setValue ("Updates/CheckAtStart", at_start->isChecked ());
  if (!chosen || chosen == later) return;
  if (chosen == skip) {settings_->setValue ("Updates/Skip", tag); return;}
  if (chosen == open_page) {QDesktopServices::openUrl (QUrl {page}); return;}
  if (chosen == install) download (tag, asset_name, asset_url, asset_size);
}

void UpdateChecker::download (QString const& tag, QString const& name, QString const& url, qint64 size)
{
#if defined (Q_OS_WIN)
  auto const folder = QStandardPaths::writableLocation (QStandardPaths::TempLocation);
#else
  auto folder = QStandardPaths::writableLocation (QStandardPaths::DownloadLocation);
  if (folder.isEmpty ()) folder = QDir::homePath ();
#endif
  QDir {}.mkpath (folder);
  auto const path = QDir {folder}.absoluteFilePath (name);
  auto * file = new QFile {path, this};
  if (!file->open (QIODevice::WriteOnly))
    {
      QMessageBox::warning (window_, tr ("KF0KYL Digi update"), tr ("Can't write %1").arg (QDir::toNativeSeparators (path)));
      delete file;
      return;
    }
  QNetworkRequest req {QUrl {url}};
  req.setHeader (QNetworkRequest::UserAgentHeader, "KF0KYL-Digi/" + current_version ());
  req.setAttribute (QNetworkRequest::FollowRedirectsAttribute, true);
  auto * reply = net_->get (req);
  auto * progress = new QProgressDialog {tr ("Downloading KF0KYL Digi %1…").arg (QString {tag}.remove (QRegularExpression {"^[vV]"})),
                                         tr ("Cancel"), 0, 100, window_};
  progress->setWindowTitle (tr ("KF0KYL Digi update"));
  progress->setMinimumDuration (0);
  progress->setAutoClose (false);
  progress->setValue (0);
  connect (reply, &QNetworkReply::readyRead, this, [reply, file] {file->write (reply->readAll ());});
  connect (reply, &QNetworkReply::downloadProgress, progress, [progress, size] (qint64 got, qint64 total) {
      if (total <= 0) total = size;
      if (total > 0) progress->setValue (int (100 * got / total));
    });
  connect (progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
  connect (reply, &QNetworkReply::finished, this, [this, reply, file, progress, path, tag] {
      file->write (reply->readAll ());
      file->close ();
      progress->close ();
      progress->deleteLater ();
      reply->deleteLater ();
      file->deleteLater ();
      if (reply->error ())
        {
          QFile::remove (path);
          if (reply->error () != QNetworkReply::OperationCanceledError)
            {
              QMessageBox::warning (window_, tr ("KF0KYL Digi update"), tr ("The download failed:\n%1").arg (reply->errorString ()));
            }
          return;
        }
      downloaded (tag, path);
    });
}

void UpdateChecker::downloaded (QString const& tag, QString const& path)
{
  auto const shown = QDir::toNativeSeparators (path);
#if defined (Q_OS_WIN)
  auto const answer = QMessageBox::question (window_, tr ("KF0KYL Digi update"),
      tr ("KF0KYL Digi %1 has downloaded.\n\nKF0KYL Digi will close and the installer will start. "
          "Your settings and log are kept.").arg (QString {tag}.remove (QRegularExpression {"^[vV]"})),
      QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok);
  if (QMessageBox::Ok != answer) return;
  if (!QProcess::startDetached (path, {}))
    {
      QMessageBox::warning (window_, tr ("KF0KYL Digi update"), tr ("Couldn't start the installer. It's saved at:\n%1").arg (shown));
      return;
    }
  QTimer::singleShot (300, window_, [this] {window_->close ();});
#else
  QString how;
  if (path.endsWith (".deb"))
    {
      how = QString {"sudo apt install \"%1\""}.arg (path);
    }
  else
    {
      QFile::setPermissions (path, QFile::permissions (path) | QFileDevice::ExeOwner | QFileDevice::ExeUser);
      how = QString {"\"%1\""}.arg (path);
    }
  QMessageBox box {QMessageBox::Information, tr ("KF0KYL Digi update"),
                   tr ("KF0KYL Digi %1 is saved at:\n%2\n\nClose KF0KYL Digi, then install it from a terminal with:\n\n%3")
                   .arg (QString {tag}.remove (QRegularExpression {"^[vV]"}), shown, how), QMessageBox::Ok, window_};
  auto * copy = box.addButton (tr ("Copy command"), QMessageBox::ActionRole);
  auto * folder = box.addButton (tr ("Open folder"), QMessageBox::ActionRole);
  box.exec ();
  if (box.clickedButton () == copy) QApplication::clipboard ()->setText (how);
  if (box.clickedButton () == folder) QDesktopServices::openUrl (QUrl::fromLocalFile (QFileInfo {path}.absolutePath ()));
#endif
}
