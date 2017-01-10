
//          Copyright Hannes Domani 2017.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file ../LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef HEOB_H
#define HEOB_H

#include <extensionsystem/iplugin.h>

#include <windows.h>


class QWinEventNotifier;


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

    void setHandle(HANDLE h);

private slots:
    bool processFinished();

private:
    HANDLE processHandle;
    QWinEventNotifier *processFinishedNotifier;
};


} // namespace Internal
} // namespace heob

#endif // HEOB_H
