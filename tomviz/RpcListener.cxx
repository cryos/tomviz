/******************************************************************************

  This source file is part of the tomviz project.

  Copyright Kitware, Inc.

  This source code is released under the New BSD License, (the "License").

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

******************************************************************************/

#include "RpcListener.h"

#include "LoadDataReaction.h"
#include "MainWindow.h"
#include "ResetReaction.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QInputDialog>

#include <QtCore/QJsonValue>
#include <QtCore/QTimer>
#include <QtCore/QDebug>

#include <molequeue/client/jsonrpcclient.h>
#include <molequeue/servercore/jsonrpc.h>
#include <molequeue/servercore/localsocketconnectionlistener.h>

namespace tomviz {

using std::string;

RpcListener::RpcListener(QObject* parent_)
  : QObject(parent_), m_pingClient(nullptr)
{
  m_rpc = new MoleQueue::JsonRpc(this);

  m_connectionListener =
    new MoleQueue::LocalSocketConnectionListener(this, "tomviz");

  connect(
    m_connectionListener,
    SIGNAL(connectionError(MoleQueue::ConnectionListener::Error, QString)),
    SLOT(connectionError(MoleQueue::ConnectionListener::Error, QString)));

  m_rpc->addConnectionListener(m_connectionListener);

  connect(m_rpc, SIGNAL(messageReceived(const MoleQueue::Message&)), this,
          SLOT(messageReceived(const MoleQueue::Message&)));

  // Find the main window.
  m_window = 0;
  foreach (QWidget* widget, QApplication::topLevelWidgets())
    if ((m_window = qobject_cast<MainWindow*>(widget)))
      break;
}

RpcListener::~RpcListener()
{
  m_rpc->removeConnectionListener(m_connectionListener);
  m_connectionListener->stop();
}

void RpcListener::start()
{
  m_connectionListener->start();
  qDebug() << "Full server path:" << m_connectionListener->fullConnectionString();
}

void RpcListener::connectionError(MoleQueue::ConnectionListener::Error error,
                                  const QString& message)
{
  qDebug() << "Error starting RPC server:" << message;
  if (error == MoleQueue::ConnectionListener::AddressInUseError) {
    // Try to ping the existing server to see if it is alive:
    if (!m_pingClient)
      m_pingClient = new MoleQueue::JsonRpcClient(this);
    bool result(
      m_pingClient->connectToServer(m_connectionListener->connectionString()));

    if (result) {
      QJsonObject request(m_pingClient->emptyRequest());
      request["method"] = QLatin1String("internalPing");
      connect(m_pingClient, SIGNAL(resultReceived(QJsonObject)),
              SLOT(receivePingResponse(QJsonObject)));
      result = m_pingClient->sendRequest(request);
    }

    // If any of the above failed, trigger a failure now:
    if (!result)
      receivePingResponse();
    else // Otherwise wait 200 ms
      QTimer::singleShot(200, this, SLOT(receivePingResponse()));
  }
}

void RpcListener::receivePingResponse(const QJsonObject& response)
{
  // Disconnect and remove the ping client the first time this is called:
  if (m_pingClient) {
    m_pingClient->deleteLater();
    m_pingClient = nullptr;
  } else {
    // In case the single shot timeout is triggered after the slot is called
    // directly or in response to m_pingClient's signal.
    return;
  }

  bool pingSuccessful = response.value("result").toString() == QString("pong");
  if (pingSuccessful) {
    qDebug() << "Other server is alive. Not starting new instance.";
  } else {
    QString title(tr("Error starting RPC server:"));
    QString label(
      tr("An error occurred while starting the RPC listener. "
         "This may be happen for a\nnumber of reasons:\n\t"
         "A previous instance of Tomviz may have crashed.\n\t"
         "A running Tomviz instance was too busy to respond.\n\n"
         "If no other Tomviz instance is running on this machine, it "
         "is safe to replace the dead\nserver. "
         "Otherwise, this instance of Tomviz may be started without "
         "RPC capabilities\n(this will prevent RPC enabled applications "
         "from communicating with Tomviz)."));
    QStringList items;
    items << tr("Replace the dead server with a new instance.");
    items << tr("Start without RPC capabilities.");
    bool ok(false);
    QString item(
      QInputDialog::getItem(nullptr, title, label, items, 0, false, &ok));

    if (ok && item == items.first()) {
      qDebug() << "Starting new server.";
      m_connectionListener->stop(true);
      m_connectionListener->start();
    } else {
      qDebug() << "Starting without RPC capabilities.";
    }
  }
}

void RpcListener::messageReceived(const MoleQueue::Message& message)
{
  QString method = message.method();
  QJsonObject params = message.params().toObject();

  if (method == "openFile") {
    // Read the supplied file.
    auto fileName = params["fileName"].toString();
    auto defaultModules = true;
    if (params.contains("defaultModules")) {
      defaultModules = params["defaultModules"].toBool(true);
    }
    auto ds = LoadDataReaction::loadData(fileName, defaultModules);
    bool success = ds != nullptr;
    if (success) {
      // set response
      MoleQueue::Message response = message.generateResponse();
      response.setResult(true);
      response.send();
    } else {
      // send error response
      MoleQueue::Message errorMessage = message.generateErrorResponse();
      errorMessage.setErrorCode(-1);
      errorMessage.setErrorMessage(
        QString("Failed to read file: %1").arg("Error"));
      errorMessage.send();
    }
  } else if (method == "reloadFile") {
    // Reload the supplied file.
    // FIXME: Need to add capability to reload a file, stub right now.
    auto fileName = params["fileName"].toString();
    bool success = !fileName.isEmpty();
    success = false;
    if (success) {
      // set response
      MoleQueue::Message response = message.generateResponse();
      response.setResult(true);
      response.send();
    } else {
      // send error response
      MoleQueue::Message errorMessage = message.generateErrorResponse();
      errorMessage.setErrorCode(-1);
      errorMessage.setErrorMessage(
        QString("Failed to reload file: %1").arg("Not implemented"));
      errorMessage.send();
    }
  } else if (method == "reset") {
    // This never fails, reset the application.
    ResetReaction::reset();
    MoleQueue::Message response = message.generateResponse();
    response.setResult(true);
    response.send();
  } else if (method == "kill") {
    // Only allow Tomviz to be killed through RPC if it was started with the
    // '--testing' flag.
    if (qApp->arguments().contains("--testing")) {
      MoleQueue::Message response = message.generateResponse();
      response.setResult(true);
      response.send();

      qApp->quit();
    } else {
      // send error response
      MoleQueue::Message errorMessage = message.generateErrorResponse();
      errorMessage.setErrorCode(-1);
      errorMessage.setErrorMessage(
        "Ignoring kill command. Start with '--testing' to enable.");
      errorMessage.send();
    }
  } else {
    MoleQueue::Message errorMessage = message.generateErrorResponse();
    errorMessage.setErrorCode(-32601);
    errorMessage.setErrorMessage("Method not found");
    QJsonObject errorDataObject;
    errorDataObject.insert("request", message.toJsonObject());
    errorMessage.setErrorData(errorDataObject);
    errorMessage.send();
  }
}

} // End of namespace
