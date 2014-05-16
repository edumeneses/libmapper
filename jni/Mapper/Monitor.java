
package Mapper;

import Mapper.NativeLib;
import Mapper.PropertyValue;
import Mapper.TimeTag;
import Mapper.Db.*;

public class Monitor
{
    /*! Bit flags for coordinating monitor metadata subscriptions. */
    public static final int SUB_DEVICE                  = 0x01;
    public static final int SUB_DEVICE_INPUTS           = 0x02;
    public static final int SUB_DEVICE_OUTPUTS          = 0x04;
    public static final int SUB_DEVICE_SIGNALS          = 0x06;
    public static final int SUB_DEVICE_LINKS_IN         = 0x08;
    public static final int SUB_DEVICE_LINKS_OUT        = 0x10;
    public static final int SUB_DEVICE_LINKS            = 0x18;
    public static final int SUB_DEVICE_CONNECTIONS_IN   = 0x20;
    public static final int SUB_DEVICE_CONNECTIONS_OUT  = 0x40;
    public static final int SUB_DEVICE_CONNECTIONS      = 0x60;
    public static final int SUB_DEVICE_ALL              = 0xFF;

    public Monitor(int autosubscribe_flags) {
        _monitor = mmon_new(autosubscribe_flags);
        Db = this.new Db(mmon_get_db(_monitor));
    }
    public Monitor() {
        _monitor = mmon_new(0);
        Db = this.new Db(mmon_get_db(_monitor));
    }

    public void free() {
        if (_monitor!=0)
            mmon_free(_monitor);
        _monitor = 0;
    }

    public int poll(int timeout) {
        return mmon_poll(_monitor, timeout);
    }

    public class Db {

        /*! The set of possible actions on a db entity. */
        public static final int MODIFIED = 0;
        public static final int NEW      = 1;
        public static final int REMOVED  = 2;

        private Db(long d) {
            _db = d;
            _device_cb = null;
            _signal_cb = null;
            _link_cb = null;
            _connection_cb = null;
        }

        private void checkMonitor() {
            if (_monitor._monitor == 0)
                throw new NullPointerException(
                    "Db object associated with invalid Monitor");
        }

        public boolean valid() {
            return _monitor._monitor != 0;
        }

        public void addDeviceCallback(Mapper.Db.DeviceListener handler)
        {
            if (handler != _device_cb)
                mdb_remove_device_callback(_db, _device_cb);
            mdb_add_device_callback(_db, handler);
            _device_cb = handler;
        }
        private native void mdb_add_device_callback(long _d, Mapper.Db.DeviceListener handler);

        public void removeDeviceCallback(Mapper.Db.DeviceListener handler)
        {
            mdb_remove_device_callback(_db, handler);
        }
        private native void mdb_remove_device_callback(long _d, Mapper.Db.DeviceListener handler);

        public void addSignalCallback(Mapper.Db.SignalListener handler)
        {
            if (handler != _signal_cb)
                mdb_remove_signal_callback(_db, _signal_cb);
            mdb_add_signal_callback(_db, handler);
            _signal_cb = handler;
        }
        private native void mdb_add_signal_callback(long _d, Mapper.Db.SignalListener handler);

        public void removeSignalCallback(Mapper.Db.SignalListener handler)
        {
            mdb_remove_signal_callback(_db, handler);
        }
        private native void mdb_remove_signal_callback(long _d, Mapper.Db.SignalListener handler);

        public void addLinkCallback(Mapper.Db.LinkListener handler)
        {
            if (handler != _link_cb)
                mdb_remove_link_callback(_db, _link_cb);
            mdb_add_link_callback(_db, handler);
            _link_cb = handler;
        }
        private native void mdb_add_link_callback(long _p, Mapper.Db.LinkListener handler);

        public void removeLinkCallback(Mapper.Db.LinkListener handler)
        {
            mdb_remove_link_callback(_db, handler);
        }
        private native void mdb_remove_link_callback(long _d, Mapper.Db.LinkListener handler);

        public void addConnectionCallback(Mapper.Db.ConnectionListener handler)
        {
            if (handler != _connection_cb)
                mdb_remove_connection_callback(_db, _connection_cb);
            mdb_add_connection_callback(_db, handler);
            _connection_cb = handler;
        }
        private native void mdb_add_connection_callback(long _d, Mapper.Db.ConnectionListener handler);

        public void removeConnectionCallback(Mapper.Db.ConnectionListener handler)
        {
            mdb_remove_connection_callback(_db, handler);
        }
        private native void mdb_remove_connection_callback(long _d, Mapper.Db.ConnectionListener handler);

        private long _db;
        private Monitor _monitor;

        // TODO: enable multiple listeners
        private Mapper.Db.DeviceListener _device_cb;
        private Mapper.Db.SignalListener _signal_cb;
        private Mapper.Db.LinkListener _link_cb;
        private Mapper.Db.ConnectionListener _connection_cb;
    };

    public void subscribe(String device_name, int subscribe_flags, int timeout)
    {
        mmon_subscribe(_monitor, device_name, subscribe_flags, timeout);
    }

    public void unsubscribe(String device_name)
    {
        mmon_unsubscribe(_monitor, device_name);
    }

    public void link(String source_device, String dest_device,
                     Mapper.Db.Link props)
    {
        mmon_link(_monitor, source_device, dest_device, props);
    }

    public void unlink(String source_device, String dest_device)
    {
        mmon_unlink(_monitor, source_device, dest_device);
    }

    public void connect(String source_signal, String dest_signal,
                        Mapper.Db.Connection props)
    {
        mmon_connect(_monitor, source_signal, dest_signal, props);
    }

    public void disconnect(String source_signal, String dest_signal)
    {
        mmon_disconnect(_monitor, source_signal, dest_signal);
    }

    public void connection_modify(String source_signal, String dest_signal,
                                  Mapper.Db.Connection props)
    {
        mmon_connection_modify(_monitor, source_signal, dest_signal, props);
    }

    public void autosubscribe(int autosubscribe_flags)
    {
        mmon_autosubscribe(_monitor, autosubscribe_flags);
    }

    public TimeTag now()
    {
        return mmon_now(_monitor);
    }

    // Note: this is _not_ guaranteed to run, the user should still
    // call free() explicitly when the monitor is no longer needed.
    protected void finalize() throws Throwable {
        try {
            free();
        } finally {
            super.finalize();
        }
    }

    private native long mmon_new(int autosubscribe_flags);
    private native long mmon_get_db(long _d);
    private native void mmon_free(long _d);
    private native int mmon_poll(long _d, int timeout);
    private native void mmon_subscribe(long _d, String device_name,
                                       int subscribe_flags, int timeout);
    private native void mmon_unsubscribe(long _d, String device_name);
    private native void mmon_link(long _d, String source_device,
                                  String dest_device, Mapper.Db.Link props);
    private native void mmon_unlink(long _d, String source_device,
                                    String dest_device);
    private native void mmon_connect(long _d, String source_signal,
                                     String dest_signal,
                                     Mapper.Db.Connection props);
    private native void mmon_connection_modify(long _d, String source_signal,
                                               String dest_signal,
                                               Mapper.Db.Connection props);
    private native void mmon_disconnect(long _d, String source_signal,
                                        String dest_signal);
    private native void mmon_autosubscribe(long _d, int autosubscribe_flags);
    private native TimeTag mmon_now(long _d);

    private long _monitor;
    public Mapper.Monitor.Db Db;
    public boolean valid() {
        return _monitor != 0;
    }

    static { 
        System.loadLibrary(NativeLib.name);
    } 
}
