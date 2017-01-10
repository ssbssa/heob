
//          Copyright Hannes Domani 2017.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file ../LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "heobplugin.h"
#include "heobconstants.h"

#include <projectexplorer/project.h>
#include <projectexplorer/session.h>
#include <projectexplorer/target.h>
#include <projectexplorer/environmentaspect.h>
#include <projectexplorer/localapplicationrunconfiguration.h>
#include <projectexplorer/kitinformation.h>
#include <projectexplorer/toolchain.h>
#include <projectexplorer/abi.h>

#include <coreplugin/icore.h>
#include <coreplugin/icontext.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/coreconstants.h>

#include <utils/fileutils.h>

#include <QWinEventNotifier>
#include <QAction>
#include <QMessageBox>
#include <QMainWindow>
#include <QMenu>
#include <QStandardPaths>

#include <QtPlugin>


using namespace heob::Internal;
using namespace ProjectExplorer;


heobPlugin::heobPlugin()
{
    // Create your members
}

heobPlugin::~heobPlugin()
{
    // Unregister objects from the plugin manager's object pool
    // Delete members
}

bool heobPlugin::initialize(const QStringList &arguments, QString *errorString)
{
    // Register objects in the plugin manager's object pool
    // Load settings
    // Add actions to menus
    // Connect to other plugins' signals
    // In the initialize function, a plugin can be sure that the plugins it
    // depends on have initialized their members.

    Q_UNUSED(arguments)
    Q_UNUSED(errorString)

    QAction *action = new QAction(tr("heob"), this);
    Core::Command *cmd = Core::ActionManager::registerAction(action, Constants::ACTION_ID,
                                                             Core::Context(Core::Constants::C_GLOBAL));
    cmd->setDefaultKeySequence(QKeySequence(tr("Ctrl+Alt+H")));
    connect(action, SIGNAL(triggered()), this, SLOT(triggerAction()));
    Core::ActionManager::actionContainer(Core::Constants::M_TOOLS)->addAction(cmd);

    return true;
}

void heobPlugin::extensionsInitialized()
{
    // Retrieve objects from the plugin manager's object pool
    // In the extensionsInitialized function, a plugin can be sure that all
    // plugins that depend on it are completely initialized.
}

ExtensionSystem::IPlugin::ShutdownFlag heobPlugin::aboutToShutdown()
{
    // Save settings
    // Disconnect from signals that are not needed during shutdown
    // Hide UI (if you add UI that is not in the main window directly)
    return SynchronousShutdown;
}

void heobPlugin::triggerAction()
{
    LocalApplicationRunConfiguration *localRc = 0;
    const ToolChain *tc = 0;
    if (Project *project = SessionManager::startupProject())
    {
        if (Target *target = project->activeTarget())
        {
            if (RunConfiguration *rc = target->activeRunConfiguration())
                localRc = qobject_cast<LocalApplicationRunConfiguration *>(rc);
            if (Kit *kit = target->kit())
                tc = ToolChainKitInformation::toolChain(kit);
        }
    }
    if (!localRc)
    {
        QMessageBox::warning(Core::ICore::mainWindow(),
                             tr("heob"),
                             tr("no local run configuration available"));
        return;
    }
    if (!tc)
    {
        QMessageBox::warning(Core::ICore::mainWindow(),
                             tr("heob"),
                             tr("no toolchain available"));
        return;
    }

    // target executable
    QString executable = localRc->executable();
    if (executable.isEmpty())
    {
        QMessageBox::warning(Core::ICore::mainWindow(),
                             tr("heob"),
                             tr("no executable set"));
        return;
    }

    // heob executable
    QString heob = QString::fromLatin1("heob%1.exe").arg(tc->targetAbi().wordWidth());
    QString heobPath = QStandardPaths::findExecutable(heob);
    if (heobPath.isEmpty())
    {
        QMessageBox::warning(Core::ICore::mainWindow(),
                             tr("heob"),
                             tr("can't find %1").arg(heob));
        return;
    }

    // working directory
    QString workingDirectory = Utils::FileUtils::normalizePathName(localRc->workingDirectory());

    // make executable a relative path if possible
    QString wdSlashed = workingDirectory + QLatin1Char('/');
    if (executable.startsWith(wdSlashed, Qt::CaseInsensitive))
        executable.remove(0, wdSlashed.size());

    // quote executable if it contains spaces
    QString exeQuote;
    if (executable.contains(QLatin1Char(' ')))
        exeQuote = QLatin1Char('\"');

    // full command line
    QString arguments = heob + QLatin1String(" -xleaks.xml -p0 -k2 ") +
            exeQuote + executable + exeQuote + QLatin1Char(' ') + localRc->commandLineArguments();
    QByteArray argumentsCopy((const char *)arguments.utf16(), arguments.size()*2+2);

    // process environment
    QByteArray env;
    void *envPtr = 0;
    if (EnvironmentAspect *environment = localRc->extraAspect<EnvironmentAspect>())
    {
        QStringList envStrings = environment->environment().toStringList();
        if (!envStrings.isEmpty())
        {
            uint pos = 0;
            foreach (QString par, envStrings)
            {
                uint parsize = par.size()*2 + 2;
                env.resize(env.size()+parsize);
                memcpy(env.data()+pos, par.utf16(), parsize);
                pos += parsize;
            }
            env.resize(env.size()+2);
            env[pos++] = 0;
            env[pos++] = 0;

            envPtr = env.data();
        }
    }

    // heob process
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    if (!CreateProcess((LPCWSTR)heobPath.utf16(), (LPWSTR)argumentsCopy.data(), 0, 0, FALSE,
                       CREATE_UNICODE_ENVIRONMENT, envPtr,
                       (LPCWSTR)workingDirectory.utf16(), &si, &pi))
    {
        QMessageBox::warning(Core::ICore::mainWindow(),
                             tr("heob"),
                             tr("can't create %1 process").arg(heob));
        return;
    }

    // heob finished signal handler
    HeobData *hd = new HeobData;
    hd->setHandle(pi.hProcess);

    CloseHandle(pi.hThread);
}


HeobData::HeobData()
{
    processHandle = 0;
    processFinishedNotifier = 0;
}

HeobData::~HeobData()
{
    if (processHandle) CloseHandle(processHandle);
    delete processFinishedNotifier;
}

void HeobData::setHandle(HANDLE h)
{
    processHandle = h;
    processFinishedNotifier = new QWinEventNotifier(h);
    connect(processFinishedNotifier, SIGNAL(activated(HANDLE)), this, SLOT(processFinished()));
    processFinishedNotifier->setEnabled(true);
}

bool HeobData::processFinished()
{
    processFinishedNotifier->setEnabled(false);
    QString exitMsg = tr("heob process finished");
    DWORD exitCode;
    if (GetExitCodeProcess(processHandle, &exitCode))
        exitMsg += QString::fromLatin1(" (exit code: %1)").arg(exitCode);
    QMessageBox::information(Core::ICore::mainWindow(),
                             tr("heob"),
                             exitMsg);
    deleteLater();
    return true;
}
