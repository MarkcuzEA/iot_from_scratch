QT -= gui

CONFIG += c++11 console
CONFIG -= app_bundle

DEFINES += QT_DEPRECATED_WARNINGS

# Change /opt/azure/ to the location where azure-iot-sdk-c is installed
#QMAKE_CXXFLAGS += -I$${TARGET_SYSROOT}/include/
#QMAKE_CXXFLAGS += -I$${TARGET_SYSROOT}/include/azureiot
#LIBS += -L/opt/azure/lib

TARGET = AzureIoTTest
target.path = /home
INSTALLS += target

contains (QMAKE_HOST.os, Windows) {
   message ("Windows Host")
   TARGET_SYSROOT = C:\QtGhSupport\GrayhillDisplayPlatform/sysroot-target
}

INCLUDEPATH += .
INCLUDEPATH += $${TARGET_SYSROOT}/kernel-headers/include
INCLUDEPATH += $${TARGET_SYSROOT}/usr/include
INCLUDEPATH += $${TARGET_SYSROOT}/usr/include/azureiot
INCLUDEPATH += $${TARGET_SYSROOT}/include

contains (hw_present, yes) {
   message( "Building target version" )
   DEFINES += ON_HARDWARE
   LIBS += -L$${TARGET_SYSROOT}/usr/lib -lghdrv -lrt -lghio \
    #-liothub_client -liothub_client_amqp_transport -laziotsharedutil -liothub_service_client -luamqp\
    -liothub_client -liothub_client_mqtt_transport -lumqtt -luhttp\
    -laziotsharedutil  -liothub_service_client\
    -lserializer  -lumock_c \
    -lcurl -lssl -lcrypto \
    -lprov_auth_client -lhsm_security_client \
    -luuid -lparson\
    -lutpm \
    -ldl
   #
   # udev is needed for CAN
   #
   LIBS += -L$${TARGET_SYSROOT}/lib -ludev
}

SOURCES += \
        main.cpp

HEADERS += \
    certs.h

