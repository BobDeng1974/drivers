# camera_hot-plug_sys_open.c
1:tasklet and workqueue
2:	client->flags |= I2C_CLIENT_PEC;
		/*
		 * Setting the PEC flag here won't affect kernel drivers,
		 * which will be using the i2c_client node registered with
		 * the driver model core.  Likewise, when that client has
		 * the PEC flag already set, the i2c-dev driver won't see
		 * (or use) this setting.
		 */
3:A hot plug can trigger three interruptions that require a sleep function



