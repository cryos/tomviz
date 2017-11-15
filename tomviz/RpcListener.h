/******************************************************************************

  This source file is part of the Avogadro project.

  Copyright 2012-2013 Kitware, Inc.

  This source code is released under the New BSD License, (the "License").

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

******************************************************************************/

#ifndef TOMVIZ_RPCLISTENER_H
#define TOMVIZ_RPCLISTENER_H

#include <QtCore/QJsonObject>
#include <QtCore/QObject>

#include <molequeue/servercore/localsocketconnectionlistener.h>

namespace MoleQueue {
class JsonRpc;
class JsonRpcClient;
class Message;
}

namespace tomviz {

class MainWindow;

/**
 * @brief The RpcListener class is used to implement the remote procedure call
 * interface for the Tomviz application.
 */

class RpcListener : public QObject
{
  Q_OBJECT

public:
  explicit RpcListener(QObject* parent = 0);
  ~RpcListener();

  void start();

private slots:
  void connectionError(MoleQueue::ConnectionListener::Error, const QString&);
  void receivePingResponse(const QJsonObject& response = QJsonObject());
  void messageReceived(const MoleQueue::Message& message);

private:
  MoleQueue::JsonRpc* m_rpc;
  MoleQueue::LocalSocketConnectionListener* m_connectionListener;
  MainWindow* m_window;
  MoleQueue::JsonRpcClient* m_pingClient;
};

} // End namespace

#endif
