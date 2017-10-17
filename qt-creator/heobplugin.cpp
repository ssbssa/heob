
//          Copyright Hannes Domani 2017.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file ../LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#define NOMINMAX

#include "heobplugin.h"
#include "heobconstants.h"

#include <projectexplorer/project.h>
#include <projectexplorer/session.h>
#include <projectexplorer/target.h>
#include <projectexplorer/environmentaspect.h>
#if QTCREATOR_MAJOR_VERSION<4
#include <projectexplorer/localapplicationrunconfiguration.h>
#else
#include <projectexplorer/runconfiguration.h>
#include <projectexplorer/runnables.h>
#endif
#include <projectexplorer/kitinformation.h>
#include <projectexplorer/toolchain.h>
#include <projectexplorer/abi.h>

#include <coreplugin/icore.h>
#include <coreplugin/icontext.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/actionmanager/actioncontainer.h>
#if QTCREATOR_MAJOR_VERSION<4
#include <analyzerbase/analyzerconstants.h>
#else
#include <debugger/analyzer/analyzerconstants.h>
#endif

#include <utils/fileutils.h>

#include <QWinEventNotifier>
#include <QAction>
#include <QMessageBox>
#include <QMainWindow>
#include <QMenu>
#include <QStandardPaths>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>

#include <QtPlugin>


using namespace heob::Internal;
using namespace ProjectExplorer;

#if QTCREATOR_MAJOR_VERSION<4
using namespace Analyzer::Constants;
#else
using namespace Debugger::Constants;
#endif


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
    Core::ActionManager::actionContainer(M_DEBUG_ANALYZER)->addAction(cmd, G_ANALYZER_REMOTE_TOOLS);

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
    RunConfiguration *rc = 0;
#if QTCREATOR_MAJOR_VERSION<4
    LocalApplicationRunConfiguration *localRc = 0;
#else
    StandardRunnable sr;
#endif
    Abi abi;
    bool hasLocalRc = false;
    if (Project *project = SessionManager::startupProject())
    {
        if (Target *target = project->activeTarget())
        {
            rc = target->activeRunConfiguration();
#if QTCREATOR_MAJOR_VERSION<4
            if (rc)
            {
                localRc = qobject_cast<LocalApplicationRunConfiguration *>(rc);
                hasLocalRc = localRc != 0;
            }
#endif
            if (Kit *kit = target->kit())
            {
#if QTCREATOR_MAJOR_VERSION<4 || (QTCREATOR_MAJOR_VERSION==4 && QTCREATOR_MINOR_VERSION<2)
                const ToolChain *tc = ToolChainKitInformation::toolChain(kit);
                if (tc)
                    abi = tc->targetAbi();
#else
                abi = ToolChainKitInformation::targetAbi(kit);
#endif

#if QTCREATOR_MAJOR_VERSION>=4
                if (rc)
                {
                    const Runnable runnable = rc->runnable();
                    if (runnable.is<StandardRunnable>())
                    {
                        sr = runnable.as<StandardRunnable>();
                        const IDevice::ConstPtr device = sr.device;
                        hasLocalRc = device && device->type() == ProjectExplorer::Constants::DESKTOP_DEVICE_TYPE;
                        if (!hasLocalRc)
                            hasLocalRc = DeviceTypeKitInformation::deviceTypeId(kit) == ProjectExplorer::Constants::DESKTOP_DEVICE_TYPE;
                    }
                }
#endif
            }
        }
    }
    if (!hasLocalRc)
    {
        QMessageBox::warning(Core::ICore::mainWindow(),
                             tr("heob"),
                             tr("no local run configuration available"));
        return;
    }
    if (abi.architecture() != Abi::X86Architecture ||
            abi.os() != Abi::WindowsOS ||
            abi.binaryFormat() != Abi::PEFormat ||
            (abi.wordWidth() != 32 && abi.wordWidth() != 64))
    {
        QMessageBox::warning(Core::ICore::mainWindow(),
                             tr("heob"),
                             tr("no toolchain available"));
        return;
    }

    QString executable;
    QString workingDirectory;
    QString commandLineArguments;
    QStringList envStrings;
#if QTCREATOR_MAJOR_VERSION<4
    executable = localRc->executable();
    workingDirectory = localRc->workingDirectory();
    commandLineArguments = localRc->commandLineArguments();
    if (EnvironmentAspect *environment = localRc->extraAspect<EnvironmentAspect>())
        envStrings = environment->environment().toStringList();
#else
    executable = sr.executable;
    workingDirectory = sr.workingDirectory;
    commandLineArguments = sr.commandLineArguments;
    envStrings = sr.environment.toStringList();
#endif

    // target executable
    if (executable.isEmpty())
    {
        QMessageBox::warning(Core::ICore::mainWindow(),
                             tr("heob"),
                             tr("no executable set"));
        return;
    }

    // heob executable
    QString heob = QString::fromLatin1("heob%1.exe").arg(abi.wordWidth());
    QString heobPath = QStandardPaths::findExecutable(heob);
    if (heobPath.isEmpty())
    {
        QMessageBox::warning(Core::ICore::mainWindow(),
                             tr("heob"),
                             tr("can't find %1").arg(heob));
        return;
    }

    // working directory
    workingDirectory = Utils::FileUtils::normalizePathName(workingDirectory);

    // make executable a relative path if possible
    QString wdSlashed = workingDirectory + QLatin1Char('/');
    if (executable.startsWith(wdSlashed, Qt::CaseInsensitive))
        executable.remove(0, wdSlashed.size());

    // heob arguments
    HeobDialog dialog(Core::ICore::mainWindow());
    if (!dialog.exec()) return;
    QString heobArguments = dialog.getArguments();

    // quote executable if it contains spaces
    QString exeQuote;
    if (executable.contains(QLatin1Char(' ')))
        exeQuote = QLatin1Char('\"');

    // full command line
    QString arguments = heob + heobArguments + QLatin1Char(' ') +
            exeQuote + executable + exeQuote;
    if (!commandLineArguments.isEmpty())
        arguments += QLatin1Char(' ') + commandLineArguments;
    QByteArray argumentsCopy((const char *)arguments.utf16(), arguments.size()*2+2);

    // process environment
    QByteArray env;
    void *envPtr = 0;
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

    // heob process
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    if (!CreateProcess((LPCWSTR)heobPath.utf16(), (LPWSTR)argumentsCopy.data(), 0, 0, FALSE,
                       CREATE_UNICODE_ENVIRONMENT|CREATE_SUSPENDED|CREATE_NEW_CONSOLE, envPtr,
                       (LPCWSTR)workingDirectory.utf16(), &si, &pi))
    {
        QMessageBox::warning(Core::ICore::mainWindow(),
                             tr("heob"),
                             tr("can't create %1 process").arg(heob));
        return;
    }

    // heob finished signal handler
    HeobData *hd = new HeobData;
    if (!hd->createErrorPipe(pi.dwProcessId))
    {
        delete hd;
        hd = 0;
    }

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (hd) hd->readExitData();
}


HeobData::HeobData()
{
    processFinishedNotifier = 0;
    errorPipe = INVALID_HANDLE_VALUE;
    ov.hEvent = NULL;
}

HeobData::~HeobData()
{
    delete processFinishedNotifier;
    if (errorPipe != INVALID_HANDLE_VALUE) CloseHandle(errorPipe);
    if (ov.hEvent) CloseHandle(ov.hEvent);
}

bool HeobData::createErrorPipe(DWORD heobPid)
{
    char errorPipeName[32];
    sprintf (errorPipeName, "\\\\.\\Pipe\\heob.error.%08X", heobPid);
    errorPipe = CreateNamedPipeA(errorPipeName,
                                 PIPE_ACCESS_INBOUND|FILE_FLAG_OVERLAPPED,
                                 PIPE_TYPE_BYTE, 1, 1024, 1024, 0, NULL);
    return errorPipe != INVALID_HANDLE_VALUE;
}

void HeobData::readExitData()
{
    ov.Offset = ov.OffsetHigh = 0;
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    bool pipeConnected = ConnectNamedPipe(errorPipe, &ov);
    if (!pipeConnected)
    {
        DWORD error = GetLastError();
        if (error == ERROR_PIPE_CONNECTED)
            pipeConnected = true;
        else if (error == ERROR_IO_PENDING)
        {
            if (WaitForSingleObject(ov.hEvent, 1000) == WAIT_OBJECT_0)
                pipeConnected = true;
            else
                CancelIo(errorPipe);
        }
    }
    if (pipeConnected)
    {
        if (ReadFile(errorPipe, exitData, sizeof(exitData), NULL, &ov) ||
                GetLastError() == ERROR_IO_PENDING)
        {
            processFinishedNotifier = new QWinEventNotifier(ov.hEvent);
            connect(processFinishedNotifier, SIGNAL(activated(HANDLE)), this, SLOT(processFinished()));
            processFinishedNotifier->setEnabled(true);
            return;
        }
    }

    delete this;
}

enum
{
    HEOB_OK,
    HEOB_HELP,
    HEOB_BAD_ARG,
    HEOB_PROCESS_FAIL,
    HEOB_WRONG_BITNESS,
    HEOB_PROCESS_KILLED,
    HEOB_NO_CRT,
    HEOB_EXCEPTION,
    HEOB_OUT_OF_MEMORY,
    HEOB_UNEXPECTED_END,
    HEOB_TRACE,
    HEOB_CONSOLE,
};

static QString upperHexNum(unsigned num)
{
    return QString::fromLatin1("0x") + QString::fromLatin1("%1").arg(num, 8, 16, QLatin1Char('0')).toUpper();
}

bool HeobData::processFinished()
{
    processFinishedNotifier->setEnabled(false);

    QString exitMsg;
    bool needsWarning = true;
    DWORD didread;
    if (GetOverlappedResult(errorPipe, &ov, &didread, TRUE) &&
            didread == sizeof(exitData))
    {
        switch (exitData[0])
        {
        case HEOB_OK:
            exitMsg = tr("heob process finished (exit code: %1, %2)").arg(exitData[1]).arg(upperHexNum(exitData[1]));
            needsWarning = false;
            break;

        case HEOB_HELP:
            exitMsg = tr("heob help screen was shown");
            break;

        case HEOB_BAD_ARG:
            exitMsg = tr("unknown argument: -%1").arg((char)exitData[1]);
            break;

        case HEOB_PROCESS_FAIL:
            exitMsg = tr("can't create process");
            break;

        case HEOB_WRONG_BITNESS:
            exitMsg = tr("wrong bitness");
            break;

        case HEOB_PROCESS_KILLED:
            exitMsg = tr("process killed");
            break;

        case HEOB_NO_CRT:
            exitMsg = tr("only works with dynamically linked CRT");
            break;

        case HEOB_EXCEPTION:
            exitMsg = tr("unhandled exception code: %1").arg(upperHexNum(exitData[1]));
            break;

        case HEOB_OUT_OF_MEMORY:
            exitMsg = tr("not enough memory to keep track of allocations");
            break;

        case HEOB_UNEXPECTED_END:
            exitMsg = tr("unexpected end of application");
            break;

        case HEOB_TRACE:
            exitMsg = tr("trace mode");
            break;

        case HEOB_CONSOLE:
            exitMsg = tr("extra console");
            break;
        }
    }
    else
    {
        exitMsg = tr("unexpected end of heob");
    }

    if (needsWarning)
        QMessageBox::warning(Core::ICore::mainWindow(),
                             tr("heob"),
                             exitMsg);
    else
        QMessageBox::information(Core::ICore::mainWindow(),
                                 tr("heob"),
                                 exitMsg);

    deleteLater();
    return true;
}


HeobDialog::HeobDialog(QWidget *parent) :
    QDialog(parent)
{
    QVBoxLayout *layout = new QVBoxLayout;
    // disable resizing
    layout->setSizeConstraint(QLayout::SetFixedSize);

    QHBoxLayout *xmlLayout = new QHBoxLayout;
    QLabel *xmlLabel = new QLabel(tr("xml output file:"));
    xmlLayout->addWidget(xmlLabel);
    xmlEdit = new QLineEdit;
    xmlEdit->setText(QLatin1String("leaks.xml"));
    xmlLayout->addWidget(xmlEdit);
    layout->addLayout(xmlLayout);

    pidWaitCheck = new QCheckBox(tr("show process ID and wait"));
    layout->addWidget(pidWaitCheck);

    QHBoxLayout *pageProtectionLayout = new QHBoxLayout;
    QLabel *pageProtectionLabel = new QLabel(tr("page protection:"));
    pageProtectionLayout->addWidget(pageProtectionLabel);
    pageProtectionCombo = new QComboBox;
    pageProtectionCombo->addItem(tr("off"));
    pageProtectionCombo->addItem(tr("after"));
    pageProtectionCombo->addItem(tr("before"));
    pageProtectionLayout->addWidget(pageProtectionCombo);
    layout->addLayout(pageProtectionLayout);

    freedProtectionCheck = new QCheckBox(tr("freed memory protection"));
    layout->addWidget(freedProtectionCheck);

    breakpointCheck = new QCheckBox(tr("raise breakpoint exception on error"));
    layout->addWidget(breakpointCheck);

    QHBoxLayout *leakDetailLayout = new QHBoxLayout;
    QLabel *leakDetailLabel = new QLabel(tr("leak details:"));
    leakDetailLayout->addWidget(leakDetailLabel);
    leakDetailCombo = new QComboBox;
    leakDetailCombo->addItem(tr("none"));
    leakDetailCombo->addItem(tr("simple"));
    leakDetailCombo->addItem(tr("detect leak types"));
    leakDetailCombo->addItem(tr("detect leak types (show reachable)"));
    leakDetailCombo->addItem(tr("fuzzy detect leak types"));
    leakDetailCombo->addItem(tr("fuzzy detect leak types (show reachable)"));
    leakDetailCombo->setCurrentIndex(1);
    leakDetailLayout->addWidget(leakDetailCombo);
    layout->addLayout(leakDetailLayout);

    QHBoxLayout *leakSizeLayout = new QHBoxLayout;
    QLabel *leakSizeLabel = new QLabel(tr("minimum leak size:"));
    leakSizeLayout->addWidget(leakSizeLabel);
    leakSizeSpin = new QSpinBox;
    leakSizeSpin->setMinimum(0);
    leakSizeSpin->setMaximum(INT_MAX);
    leakSizeSpin->setSingleStep(1000);
    leakSizeSpin->setValue(0);
    leakSizeLayout->addWidget(leakSizeSpin);
    layout->addLayout(leakSizeLayout);

    QHBoxLayout *leakRecordingLayout = new QHBoxLayout;
    QLabel *leakRecordingLabel = new QLabel(tr("control leak recording:"));
    leakRecordingLayout->addWidget(leakRecordingLabel);
    leakRecordingCombo = new QComboBox;
    leakRecordingCombo->addItem(tr("off"));
    leakRecordingCombo->addItem(tr("on (start disabled)"));
    leakRecordingCombo->addItem(tr("on (start enabled)"));
    leakRecordingCombo->setCurrentIndex(2);
    leakRecordingLayout->addWidget(leakRecordingCombo);
    layout->addLayout(leakRecordingLayout);

    QHBoxLayout *extraArgsLayout = new QHBoxLayout;
    QLabel *extraArgsLabel = new QLabel(tr("extra arguments:"));
    extraArgsLayout->addWidget(extraArgsLabel);
    extraArgsEdit = new QLineEdit;
    extraArgsLayout->addWidget(extraArgsEdit);
    layout->addLayout(extraArgsLayout);

    QHBoxLayout *okLayout = new QHBoxLayout;
    okLayout->addStretch(1);
    QPushButton *okButton = new QPushButton(tr("OK"));
    connect(okButton, &QAbstractButton::clicked, this, &QDialog::accept);
    okLayout->addWidget(okButton);
    okLayout->addStretch(1);
    layout->addLayout(okLayout);

    setLayout(layout);

    setWindowTitle(tr("heob"));

    // disable context help button
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
}

QString HeobDialog::getArguments()
{
    QString args;

    args += QLatin1String(" -A");

    QString xml = xmlEdit->text();
    if (!xml.isEmpty()) args += QLatin1String(" -x") +
            xml.replace(QLatin1Char(' '), QLatin1Char('_'));

    int pidWait = pidWaitCheck->isChecked() ? 1 : 0;
    args += QString::fromLatin1(" -P%1").arg(pidWait);

    int pageProtection = pageProtectionCombo->currentIndex();
    args += QString::fromLatin1(" -p%1").arg(pageProtection);

    int freedProtection = freedProtectionCheck->isChecked() ? 1 : 0;
    args += QString::fromLatin1(" -f%1").arg(freedProtection);

    int breakpoint = breakpointCheck->isChecked() ? 1 : 0;
    args += QString::fromLatin1(" -r%1").arg(breakpoint);

    int leakDetail = leakDetailCombo->currentIndex();
    args += QString::fromLatin1(" -l%1").arg(leakDetail);

    int leakSize = leakSizeSpin->value();
    args += QString::fromLatin1(" -z%1").arg(leakSize);

    int leakRecording = leakRecordingCombo->currentIndex();
    args += QString::fromLatin1(" -k%1").arg(leakRecording);

    QString extraArgs = extraArgsEdit->text();
    if (!extraArgs.isEmpty()) args += QLatin1Char(' ') + extraArgs;

    return args;
}
