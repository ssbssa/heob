
//          Copyright Hannes Domani 2017.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file ../LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef HEOB_H
#define HEOB_H

#include <extensionsystem/iplugin.h>

#include <QDialog>

#include <windows.h>


class QWinEventNotifier;
class QLineEdit;
class QCheckBox;
class QComboBox;
class QSpinBox;


namespace heob {
namespace Internal {

class heobPlugin : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "heob.json")

public:
    heobPlugin();
    ~heobPlugin();

    bool initialize(const QStringList &arguments, QString *errorString);
    void extensionsInitialized();
    ShutdownFlag aboutToShutdown();

private slots:
    void triggerAction();
};


class HeobData : public QObject
{
    Q_OBJECT

public:
    HeobData();
    ~HeobData();

    bool createErrorPipe(DWORD heobPid);
    void readExitData();

private slots:
    bool processFinished();

private:
    HANDLE errorPipe;
    OVERLAPPED ov;
    unsigned exitData[2];
    QWinEventNotifier *processFinishedNotifier;
};


class HeobDialog : public QDialog
{
    Q_OBJECT

public:
    HeobDialog(QWidget *parent);

    QString getArguments();

private:
    QLineEdit *xmlEdit;
    QCheckBox *pidWaitCheck;
    QComboBox *handleExceptionCombo;
    QComboBox *pageProtectionCombo;
    QCheckBox *freedProtectionCheck;
    QCheckBox *breakpointCheck;
    QComboBox *leakDetailCombo;
    QSpinBox *leakSizeSpin;
    QComboBox *leakRecordingCombo;
    QLineEdit *extraArgsEdit;
};


} // namespace Internal
} // namespace heob

#endif // HEOB_H
