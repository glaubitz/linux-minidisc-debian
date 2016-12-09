#include <QDebug>
#include <QList>
#include <QWidget>
#include "qhimddetection.h"

#include <windows.h>
#include <dbt.h>
#include <initguid.h>       // needed to handle GUIDs

#include <ntddstor.h>   // needed for handling storage devices
#include <cfgmgr32.h>   // needed for CM_Get_Child function

static const GUID my_GUID_IO_MEDIA_ARRIVAL =
    {0xd07433c0, 0xa98e, 0x11d2, {0x91, 0x7a, 0x00, 0xa0, 0xc9, 0x06, 0x8f, 0xf3} };

static const GUID my_GUID_IO_MEDIA_REMOVAL =
    {0xd07433c1, 0xa98e, 0x11d2, {0x91, 0x7a, 0x00, 0xa0, 0xc9, 0x06, 0x8f, 0xf3} };

static const GUID my_GUID_DEVINTERFACE_USB_DEVICE =
    {0xa5dcbf10, 0x6530, 0x11d2, {0x90, 0x1f, 0x00, 0xc0,0x4f, 0xb9, 0x51, 0xed} };

static const int my_DBT_CUSTOMEVENT = 0x8006;


static bool is_himddevice(QString devID, QString & name);
static QString get_deviceID_from_driveletter(char i);
static bool identified(QString devpath, QString & name);
static QString FindPath(unsigned long unitmask);

class QHiMDWinDetection : public QHiMDDetection, private QWidget {

public:
    void scan_for_himd_devices();
    QHiMDWinDetection(QObject * parent = NULL);
    ~QHiMDWinDetection();

private:
    HDEVNOTIFY hDevNotify;
    HDEVNOTIFY listen_usbdev;
    QMDDevice *find_by_handle(HANDLE devhandle);
    void add_himddevice(QString path, QString name);
    virtual void remove_himddevice(QString path);
    void add_himd(HANDLE devhandle);
    void remove_himd(HANDLE devhandle);
    HDEVNOTIFY register_mediaChange(HANDLE devhandle);
    void unregister_mediaChange(HDEVNOTIFY himd_change);
    HDEVNOTIFY register_usbDeviceNotification();
    void unregister_usbDeviceNotification();
    bool nativeEvent(const QByteArray & eventType, void * message, long *result);
    bool winEvent(MSG * msg, long * result);
};


QHiMDDetection * createDetection(QObject * parent)
{
    return new QHiMDWinDetection(parent);
}

QHiMDWinDetection::QHiMDWinDetection(QObject * parent) 
  : QHiMDDetection(parent), QWidget(0)
{
    // ask for Window ID to have Qt create the window.
    (void)winId();
    // register for usb device notifications
    if((listen_usbdev = register_usbDeviceNotification()) == NULL)
        qDebug() << "cannot register usb device notifications" << endl;
}

QHiMDWinDetection::~QHiMDWinDetection()
{
    unregister_usbDeviceNotification();
    clearDeviceList();
    cleanup_netmd_list();
}

void QHiMDWinDetection::scan_for_himd_devices()
{
    unsigned long drives = GetLogicalDrives();
    char drive[] = "A:\\";
    QString name, devID;

    for (; drive[0] <= 'Z'; ++drive[0])
    {
        if (drives & 0x1)
        {
            if(GetDriveTypeA(drive) == DRIVE_REMOVABLE)
            {
                devID = get_deviceID_from_driveletter(drive[0]);
                if(!devID.isEmpty() && !devID.contains("Floppy", Qt::CaseInsensitive))
                {
                    if(is_himddevice(devID, name))
                        add_himddevice(QString(drive[0]) + ":/", name);
                }
            }
        }
        drives = drives >> 1;
    }
    return;
}

QMDDevice *QHiMDWinDetection::find_by_handle(HANDLE devhandle)
{
    QMDDevice *mddev;

    foreach(mddev, dlist)
    {
        if(mddev->deviceType() != HIMD_DEVICE)
            continue;
        if(mddev->deviceHandle() == devhandle)
            return mddev;
    }

    return NULL;
}

static QString get_deviceID_from_driveletter(char i)
{
    char subkey[] = "\\DosDevices\\X:";
    DWORD valuesize;
    HKEY key;
    int res;
    QString devname;

    subkey[12] = i;
    res = RegOpenKeyExA( HKEY_LOCAL_MACHINE, "SYSTEM\\MountedDevices", NULL, KEY_QUERY_VALUE, &key);
    if(res != ERROR_SUCCESS)
        return QString();

    if(RegQueryValueExA(key, subkey, NULL, NULL, NULL, &valuesize) == ERROR_SUCCESS)
    {
        char *value = new char[valuesize];
        res = RegQueryValueExA(key, subkey, NULL, NULL,(LPBYTE) value, &valuesize);
        if(res == ERROR_SUCCESS)
        {
            devname = QString::fromUtf16((ushort*)value);
            devname.remove(0,4);                            // modify devname to make a valid device ID
            devname.truncate(devname.indexOf("{") -1);
            devname = devname.toUpper();
            devname.replace("#", "\\");
        }
        delete[] value;
    }
    RegCloseKey(key);
    return devname;
}

static bool is_himddevice(QString devID, QString & name)
{
    DEVINST devinst;
    DEVINST devinstparent;
    unsigned long buflen;
    QString recname, devicepath;

    CM_Locate_DevNodeA(&devinst, devID.toLatin1().data(), NULL);
    CM_Get_Parent(&devinstparent, devinst, NULL);

    if(devID.contains("RemovableMedia", Qt::CaseInsensitive))    // on Windows XP: get next parent device instance
        CM_Get_Parent(&devinstparent, devinstparent, NULL);

    CM_Get_Device_ID_Size(&buflen, devinstparent, 0);
    wchar_t *buffer = new wchar_t[buflen];
    CM_Get_Device_ID(devinstparent, buffer, buflen, 0);
    devicepath = QString::fromWCharArray(buffer);
    delete[] buffer;

    if(identified( devicepath, recname))
    {
        name = recname;
        return true;
    }
    return false;
}

static bool identified(QString devpath, QString & name)
{
    int vid = devpath.mid(devpath.indexOf("VID") + 4, 4).toInt(NULL,16);
    int pid = devpath.mid(devpath.indexOf("PID") + 4, 4).toInt(NULL,16);
    const char * devname = identify_usb_device(vid, pid);
    if (devname)
    {
        name = devname;
        return true;
    }
    return false;
}

void QHiMDWinDetection::add_himddevice(QString path, QString name)
{
    if (find_by_path(path))
        return;

    QHiMDDevice * new_device = new QHiMDDevice();
    int k;
    char drv[] = "\\\\.\\X:";
    QByteArray device = "\\\\.\\PHYSICALDRIVE";
    char file[] = "X:\\HI-MD.IND";
    DWORD retbytes;
    HANDLE hdev, dev;
    STORAGE_DEVICE_NUMBER sdn;
    OFSTRUCT OFfile;

    drv[4] = path.at(0).toLatin1();

    hdev = CreateFileA(drv, NULL , FILE_SHARE_READ, NULL,
                                           OPEN_EXISTING, 0, NULL);
    if(hdev == INVALID_HANDLE_VALUE)
        return;

    k = DeviceIoControl(hdev, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn), &retbytes, NULL);
    CloseHandle(hdev);
    if(k != 0)
        device.append(QString::number(sdn.DeviceNumber));

    dev = CreateFileA(device.data(), NULL , FILE_SHARE_READ, NULL,
                                           OPEN_EXISTING, 0, NULL);
    if(dev == INVALID_HANDLE_VALUE)
        return;

    new_device->setDeviceHandle(dev);
    new_device->registerMdChange(register_mediaChange(new_device->deviceHandle()));
    new_device->setBusy(false);
    new_device->setPath(path);
    new_device->setName(name);

    file[0] = path.at(0).toLatin1();
    if(OpenFile(file, &OFfile, OF_EXIST) != HFILE_ERROR)
        new_device->setMdInserted(true);
    else
        new_device->setMdInserted(false);

    dlist.append(new_device);
    emit deviceListChanged(dlist);

    return;
}

void QHiMDWinDetection::remove_himddevice(QString path)
{
    int index = -1;
    QHiMDDevice * dev = static_cast<QHiMDDevice *>(find_by_path(path));

    if (!dev)
        return;

    index = dlist.indexOf(dev);

    if(dev->isOpen())
        dev->close();

    if(dev->name() != "disc image")
    {
        if(dev->MdChange() != NULL)
            unregister_mediaChange((HDEVNOTIFY)dev->MdChange());
        if(dev->deviceHandle() != NULL)
             CloseHandle(dev->deviceHandle());
     }

    delete dev;
    dev = NULL;

    dlist.removeAt(index);

    emit deviceListChanged(dlist);
}

void QHiMDWinDetection::add_himd(HANDLE devhandle)
{
    QMDDevice * dev = find_by_handle(devhandle);
    if (!dev)
        return;

    if(!dev->mdInserted())
        dev->setMdInserted(true);

    if(!dev->isOpen())
        dev->open();

    return;
}

void QHiMDWinDetection::remove_himd(HANDLE devhandle)
{
    QMDDevice * dev = find_by_handle(devhandle);
    if (!dev)
        return;

    if(dev->isOpen())
        dev->close();

    dev->setMdInserted(false);

    return;
}

HDEVNOTIFY QHiMDWinDetection::register_mediaChange(HANDLE devhandle)
{
    DEV_BROADCAST_HANDLE filter;

    ZeroMemory( &filter, sizeof(filter) );
    filter.dbch_size = sizeof(DEV_BROADCAST_HANDLE);
    filter.dbch_devicetype = DBT_DEVTYP_HANDLE;
    filter.dbch_handle = devhandle;
    filter.dbch_eventguid = my_GUID_IO_MEDIA_ARRIVAL;  // includes GUID_IO_MEDIA_REMOVAL notification

    return RegisterDeviceNotification( (HWND)this->winId(), &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

}

void QHiMDWinDetection::unregister_mediaChange(HDEVNOTIFY himd_change)
{
    if(himd_change != NULL)
        UnregisterDeviceNotification(himd_change);
}

HDEVNOTIFY QHiMDWinDetection::register_usbDeviceNotification()
{
    DEV_BROADCAST_DEVICEINTERFACE filter;

    ZeroMemory(&filter, sizeof(filter));
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_reserved = 0;
    filter.dbcc_classguid = my_GUID_DEVINTERFACE_USB_DEVICE;

    return RegisterDeviceNotification( (HWND)this->winId(), &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

}

void QHiMDWinDetection::unregister_usbDeviceNotification()
{
    if(listen_usbdev != NULL)
       UnregisterDeviceNotification(listen_usbdev);
}

bool QHiMDWinDetection::nativeEvent(const QByteArray & eventType, void * message, long *result)
{
    if (eventType == "windows_generic_MSG")
        return winEvent(reinterpret_cast<MSG*>(message), result);

    return false;
}

bool QHiMDWinDetection::winEvent(MSG * msg, long * result)
    {
        QString name, devID, path ;
        if(msg->message == WM_DEVICECHANGE)
        {
            PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR )msg->lParam;
            switch(msg->wParam)
            {
                case DBT_DEVICEARRIVAL :
                {
                    if(pHdr->dbch_devicetype == DBT_DEVTYP_VOLUME)
                    {
                        PDEV_BROADCAST_VOLUME pHdrv = (PDEV_BROADCAST_VOLUME)pHdr;
                        path = FindPath(pHdrv->dbcv_unitmask);
                        devID = get_deviceID_from_driveletter(path.at(0).toLatin1());
                        if(!devID.isEmpty())
                        {
                            if(is_himddevice(devID, name))
                            {
                                qDebug() << "Message:DBT_DEVICEARRIVAL for drive " + path;
                                add_himddevice(path, name);
                            }
                        }
                    }
                    else if(pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
                    {
                        PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
                        devID = QString::fromWCharArray(pDevInf->dbcc_name).toUpper();
                        /* only handle netmd devices, himd devices will be handled by DBT_DEVTYP_VOLUME */
                        if(identified(devID, name) && name.contains("NetMD)"))
                        {
                            qDebug() << name << " detected, rescanning netmd devices" << endl;
                            rescan_netmd_devices();
                        }
                    }
                    break;
                }
                case DBT_DEVICEREMOVECOMPLETE :
                {
                    if(pHdr->dbch_devicetype == DBT_DEVTYP_VOLUME)
                    {
                        PDEV_BROADCAST_VOLUME pHdrv = (PDEV_BROADCAST_VOLUME)pHdr;
                        path = FindPath(pHdrv->dbcv_unitmask);
                        qDebug() << "Message:DBT_DEVICEREMOVECOMPLETE for drive " + path;
                        remove_himddevice(path);
                    }
                    else if(pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
                    {
                        PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
                        devID = QString::fromWCharArray(pDevInf->dbcc_name).toUpper();
                        if(identified(devID, name) && name.contains("NetMD)"))
                        {
                            qDebug() << name << " removed, rescanning netmd devices" << endl;
                            rescan_netmd_devices();
                        }
                    }
                    break;
                }
                case DBT_DEVICEQUERYREMOVE :
                {
                    if(pHdr->dbch_devicetype & DBT_DEVTYP_HANDLE)
                    {
                        PDEV_BROADCAST_HANDLE pHdrh = (PDEV_BROADCAST_HANDLE)pHdr;
                        QMDDevice *dev = find_by_handle(pHdrh->dbch_handle);
                        if(!dev)
                        {
                            qDebug() << "Message:DBT_DEVICEQUERYREMOVE for unknown device " << pHdrh->dbch_handle;
                            break;
                        }
                        if(dev->isBusy())
                        {
                            *result = BROADCAST_QUERY_DENY;
                            qDebug() << "Message:DBT_DEVICEQUERYREMOVE for drive " + path + " denied: transfer in progress";
                            return true;
                        }
                        else
                        {
                            qDebug() << "Message:DBT_DEVICEQUERYREMOVE requested";
                            remove_himddevice(dev->path());
                        }
                    }
                    else if(pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
                    {
                        PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
                        devID = QString::fromWCharArray(pDevInf->dbcc_name).toUpper();
                        if(identified(devID, name) && name.contains("NetMD)"))
                        {
                            QMDDevice * dev = find_by_name(name);
                            if(!dev)
                                break;
                            if(dev->isBusy())
                            {
                                *result = BROADCAST_QUERY_DENY;
                                return true;
                            }
                            dev->close();
                        }
                    }
                    break;
                }
                case my_DBT_CUSTOMEVENT :
                {
                    if(pHdr->dbch_devicetype & DBT_DEVTYP_HANDLE)
                    {
                        PDEV_BROADCAST_HANDLE pHdrh = (PDEV_BROADCAST_HANDLE)pHdr;
                        if (pHdrh->dbch_eventguid == my_GUID_IO_MEDIA_ARRIVAL)
                        {
                            qDebug() << "Message:DBT_CUSTOMEVENT - GUID_IO_MEDIA_ARRIVAL";
                            add_himd(pHdrh->dbch_handle);
                            break;
                        }
                        if (pHdrh->dbch_eventguid == my_GUID_IO_MEDIA_REMOVAL)
                        {
                            qDebug() << "Message:DBT_CUSTOMEVENT - GUID_IO_MEDIA_REMOVAL";
                            remove_himd(pHdrh->dbch_handle);
                            break;
                        }
                    }
                    break;
                }
                default: return false;  // skip unknown/unused messages
            }
            *result = TRUE;
            return true;
        }
        return false;
    }

static QString FindPath (unsigned long unitmask)
{
   char i;

   for (i = 0; i < 26; ++i)
   {
      if (unitmask & 0x1)
         break;
      unitmask = unitmask >> 1;
   }
   return QString(i + 'A') + ":/";
}
