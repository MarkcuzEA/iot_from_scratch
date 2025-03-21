/*
* Copyright (c) 2020 basysKom GmbH
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <azureiot/iothub.h>
#include <azureiot/iothub_device_client.h>
#include <azureiot/iothub_device_client_ll.h>
#include <azureiot/iothub_client_options.h>
#include <azureiot/iothub_message.h>

#include <azure_c_shared_utility/shared_util_options.h>
#include <azureiot/iothubtransportmqtt.h>
#include <azureiot/iothubtransport_mqtt_common.h>

#include "certs.h"

//#include <azureiot/iothub_client_authorization.h>
//#include <azureiot/iothub_transport_ll_private.h>

//#include <azureiot/azure_umqtt_c/mqtt_client.h>
//#include <azureiot/azure_umqtt_c/mqtt_message.h>

//#include <azureiot/iothubtransportamqp.h>

class IotHubClient : public QObject
{
    Q_OBJECT

public:
    IotHubClient(QObject *parent = nullptr);
    ~IotHubClient();

    bool init(const QString &connectionString);

    bool sendMessage(int id, const QByteArray &data);
    bool updateDeviceTwin(const QJsonObject &reported);

    bool connected() const;

signals:
    void connectedChanged(bool connected);
    void deviceTwinUpdated(int statusCode);
    void messageStatusChanged(int id, bool success);
    void desiredObjectChanged(QJsonObject desired);

private:
    // Callbacks for the IoT Hub client
    static void connectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result,
                                         IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason,
                                         void* context);
    static void sendConfirmCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* context);
    static void deviceTwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char* payload,
                                   size_t size, void* context);
    static void reportedStateCallback(int statusCode, void* context);

    // Call the IOT SDK's DoWork function
    void doWork();

    void setConnected(bool connected);

    struct MessageContext {
        MessageContext(IotHubClient *client, int id, IOTHUB_MESSAGE_HANDLE message) {
            this->client = client;
            this->id = id;
            this->message = message;
        }
        IotHubClient *client;
        int id;
        IOTHUB_MESSAGE_HANDLE message;
    };

    QTimer mDoWorkTimer;
	IOTHUB_DEVICE_CLIENT_LL_HANDLE mDeviceHandle = nullptr;
    bool mConnected = false;
};

IotHubClient::IotHubClient(QObject *parent)
    : QObject(parent)
{
    QObject::connect(&mDoWorkTimer, &QTimer::timeout, this, &IotHubClient::doWork);
    mDoWorkTimer.setInterval(200);
}

IotHubClient::~IotHubClient()
{
    mDoWorkTimer.stop();
    if (mDeviceHandle)
		IoTHubDeviceClient_LL_Destroy(mDeviceHandle);
    IoTHub_Deinit();
}

void IotHubClient::doWork()
{
	if (mDeviceHandle) {
		IoTHubDeviceClient_LL_DoWork(mDeviceHandle);
		//qDebug() << "Doing work...";
	}
}

bool IotHubClient::connected() const
{
    return mConnected;
}

void IotHubClient::setConnected(bool connected)
{
    if (mConnected != connected) {
        mConnected = connected;
        emit connectedChanged(mConnected);
    }
}

bool IotHubClient::init(const QString &connectionString)
{
    if (mDeviceHandle) {
        qDebug() << "Client is already initialized";
        return false;
    }

    auto result = IoTHub_Init();

    if (result != IOTHUB_CLIENT_OK) {
        qWarning() << "IoTHub_Init failed with result" << result;
		return false;
    }

	mDeviceHandle = IoTHubDeviceClient_LL_CreateFromConnectionString(connectionString.toUtf8().constData(),
																	 MQTT_Protocol);

	result = 1;

    if (!mDeviceHandle) {
		qDebug() << "Failed to create client from connection string";
        return false;
    }

	result = IoTHubDeviceClient_LL_SetConnectionStatusCallback(mDeviceHandle, connectionStatusCallback, this);

    if (result != IOTHUB_CLIENT_OK) {
		qDebug() << "Failed to set connection status callback with result" << result;
        return false;
    }

	qDebug() << "Setting conenction callback" <<result;

    result = IoTHubDeviceClient_LL_SetDeviceTwinCallback(mDeviceHandle, deviceTwinCallback, this);

    if (result != IOTHUB_CLIENT_OK) {
		qDebug() << "Failed to set device twin callback with result" << result;
        return false;
    }

	//https://github.com/Azure/azure-iot-sdk-c/blob/main/iothub_client/samples/iotedge_downstream_device_sample/iotedge_downstream_device_sample.c
	//https://github.com/Azure/azure-iot-sdk-c/issues/1693
	//IoTHubDeviceClient_SetOption(mDeviceHandle, OPTION_TRUSTED_CERT, cert_string);

	 IoTHubDeviceClient_LL_SetOption(mDeviceHandle, OPTION_TRUSTED_CERT, certificates);

    mDoWorkTimer.start();

    return true;
}

void IotHubClient::connectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result,
                                            IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason,
                                            void *context)
{
    qDebug() << "Connection status changed to" << MU_ENUM_TO_STRING(IOTHUB_CLIENT_CONNECTION_STATUS, result) <<
                "with reason" << MU_ENUM_TO_STRING(IOTHUB_CLIENT_CONNECTION_STATUS_REASON, reason);

    const auto client = static_cast<IotHubClient *>(context);

    client->setConnected(result == IOTHUB_CLIENT_CONNECTION_AUTHENTICATED);
}

void IotHubClient::sendConfirmCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *context)
{
    const auto messageContext = static_cast<IotHubClient::MessageContext *>(context);

    qDebug() << "Send confirm callback for message" << messageContext->id << "with" <<
                MU_ENUM_TO_STRING(IOTHUB_CLIENT_CONFIRMATION_RESULT, result);

    emit messageContext->client->messageStatusChanged(messageContext->id, result == IOTHUB_CLIENT_CONFIRMATION_OK);

    IoTHubMessage_Destroy(messageContext->message);
    delete messageContext;
}

void IotHubClient::deviceTwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char *payload,
                                      size_t size, void *context)
{
    qDebug() << "Received" << (updateState == DEVICE_TWIN_UPDATE_PARTIAL ? "partial" : "full") << "device twin update";

    const auto client = static_cast<IotHubClient *>(context);

    const auto data = QByteArray::fromRawData(reinterpret_cast<const char *>(payload), size);

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error) {
        qWarning() << "Failed to parse JSON document:" << parseError.errorString();
        return;
    }

    if (!document.isObject()) {
        qWarning() << "JSON document is not an object";
        return;
    }

    const auto deviceTwinObject = document.object();
    qDebug() << "Device twin content:" << deviceTwinObject;

    auto desired = deviceTwinObject;

    // Partial updates contain the desired object without an enclosing object
    if (updateState == DEVICE_TWIN_UPDATE_COMPLETE) {
        const auto jsonIterator = deviceTwinObject.constFind(QLatin1String("desired"));
        if (jsonIterator == deviceTwinObject.constEnd() || !jsonIterator->isObject()) {
            qWarning() << "The desired property is missing or invalid";
            return;
        }
        desired = jsonIterator->toObject();
    }

    emit client->desiredObjectChanged(desired);
}

void IotHubClient::reportedStateCallback(int statusCode, void *context)
{
    const auto client = static_cast<IotHubClient *>(context);
    emit client->deviceTwinUpdated(statusCode);
}

bool IotHubClient::sendMessage(int id, const QByteArray &data)
{
    const auto message = IoTHubMessage_CreateFromByteArray(
                reinterpret_cast<const unsigned char *>(data.constData()), data.size());

    const auto context = new MessageContext(this, id, message);

    const auto result = IoTHubDeviceClient_LL_SendEventAsync(mDeviceHandle, message, sendConfirmCallback, context);

    if (result != IOTHUB_CLIENT_OK) {
        IoTHubMessage_Destroy(message);
        delete context;
    }

    return result == IOTHUB_CLIENT_OK;
}

bool IotHubClient::updateDeviceTwin(const QJsonObject &reported)
{
    const auto reportedDocument = QJsonDocument(reported).toJson();
    const auto result = IoTHubDeviceClient_LL_SendReportedState(
                mDeviceHandle,
                reinterpret_cast<const unsigned char *>(reportedDocument.constData()),
                reportedDocument.size(), reportedStateCallback, this);
    return result == IOTHUB_CLIENT_OK;
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

	IotHubClient client(&a);

	const auto connectionString = QLatin1String("HostName=RUTM11Hub.azure-devices.net;DeviceId=marcusthisone;SharedAccessKey=IVnmbuloyVDwrdxnmpxdJ/af48JntQo95wGlSfGcXmg=");
	//const auto connectionString = QLatin1String("HostName=RUTM11HuB.azure-devices.net;DeviceId=marcusthisone;SharedAccessKey=zEI3oi0W5myndqwhtQnFGg/GbPWofw6wSTtKWmcjAQo=");
	auto result = client.init(connectionString);

	qDebug() << "Init:" << (result ? "successful" : "failed");

	if (!result)
		return 1;

	QObject::connect(&client, &IotHubClient::connectedChanged, [](bool connected) {
		qDebug() << "Client is now" << (connected ? "connected" : "disconnected");
	});

	QObject::connect(&client, &IotHubClient::deviceTwinUpdated, [](int statusCode) {
		qDebug() << "Device twin update returned" << statusCode;
	});

	QObject::connect(&client, &IotHubClient::messageStatusChanged, [](int id, bool success) {
		qDebug() << "Message" << id << (success ? "successfully sent" : "failed to send");
	});

    QTimer sendMessageTimer;
    int messageId = 0;

    QObject::connect(&sendMessageTimer, &QTimer::timeout, &client, [&]() {
        qDebug() << "Message send timer triggered" << QDateTime::currentDateTime().toString(Qt::ISODate);
        QJsonObject body;
        body[QStringLiteral("messageNumber")] = ++messageId;
        const auto data = QJsonDocument(body).toJson(QJsonDocument::JsonFormat::Compact);
        qDebug() << "Send data:" << data;
        client.sendMessage(messageId, data);
    });


    QObject::connect(&client, &IotHubClient::desiredObjectChanged, [&](const QJsonObject &desired) {
	   qDebug() << "in connect devicetwin";
		const auto sendIntervalKey = QLatin1String("sendInterval");

        auto jsonIterator = desired.constFind(sendIntervalKey);

        if (jsonIterator != desired.constEnd() && jsonIterator->isDouble() && jsonIterator->toInt(-1) > 0) {
            const auto sendInterval = jsonIterator->toInt();

            qDebug() << "Got send interval" << sendInterval << "from device twin";

            QJsonObject reported;
            reported[sendIntervalKey] = sendInterval;

            client.updateDeviceTwin(reported);

            // Initialize the send timer with the new interval
			 sendMessageTimer.start(1000 * sendInterval);
        }
	});

    return a.exec();
}

#include "main.moc"
