{
    .need_standby_mode = 1,
    .flashprobeinfo =
	{
		.flash_name = "ocp81373",
		.slave_write_address = 0xc6,
		.flash_id_address = 0x0C,
		.flash_id = 0x3A,
		.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
		.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
	},
    .cci_client =
	{
        .cci_i2c_master = MASTER_0,
        .i2c_freq_mode = I2C_FAST_MODE,
        .sid = 0xc6 >> 1,
	},
	.flashinitsettings =
	{
		.reg_setting =
		{

			{.reg_addr = 0x01, .reg_data = 0x00, .delay = 0x00, .data_mask = 0x00}, \
			{.reg_addr = 0x03, .reg_data = 0xBF, .delay = 0x00, .data_mask = 0x00}, \
			{.reg_addr = 0x04, .reg_data = 0x3F, .delay = 0x00, .data_mask = 0x00}, \
			{.reg_addr = 0x05, .reg_data = 0xBF, .delay = 0x00, .data_mask = 0x00}, \
			{.reg_addr = 0x06, .reg_data = 0x3F, .delay = 0x00, .data_mask = 0x00}, \
			{.reg_addr = 0x08, .reg_data = 0x1F, .delay = 0x00, .data_mask = 0x00}, \
		},
		.size = 6,
		.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
		.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
		.delay = 1,
	},
	.flashhighsettings =
	{
		.reg_setting =
		{
			{.reg_addr = 0x01, .reg_data = 0x0F, .delay = 0x00, .data_mask = 0x00}, \
			{.reg_addr = 0x03, .reg_data = 0xBF, .delay = 0x00, .data_mask = 0x00}, \
			{.reg_addr = 0x04, .reg_data = 0x3F, .delay = 0x00, .data_mask = 0x00}, \
		},
		.size = 3,
		.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
		.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
		.delay = 1,
	},
	.flashlowsettings =
	{
		.reg_setting =
		{
			{.reg_addr = 0x01, .reg_data = 0x0B, .delay = 0x00, .data_mask = 0x00}, \
			{.reg_addr = 0x05, .reg_data = 0xBF, .delay = 0x00, .data_mask = 0x00}, \
			{.reg_addr = 0x06, .reg_data = 0x3F, .delay = 0x00, .data_mask = 0x00}, \
		},
		.size = 3,
		.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
		.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
		.delay = 1,
	},
	.flashoffsettings =
	{
		.reg_setting =
		{
			{.reg_addr = 0x01, .reg_data = 0x80, .delay = 0x00, .data_mask = 0x00}, \
		},
		.size = 1,
		.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
		.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
		.delay = 1,
	},
	.flashpowerupsettings =
	{
		.single_power =
		{
			{
				.seq_type = SENSOR_VIO,
				.config_val = 1,
				.delay = 1,
			}, \
			{
				.seq_type = SENSOR_CUSTOM_GPIO1,
				.config_val = 1,
				.delay = 1,
			},
		},
		.size = 2,
	},
	.flashpowerdownsettings =
	{
		.single_power =
		{
			{
				.seq_type = SENSOR_CUSTOM_GPIO1,
				.config_val = 0,
				.delay = 1,
			}, \
			{
				.seq_type = SENSOR_VIO,
				.config_val = 0,
				.delay = 1,
			},
		},
		.size = 2,
	},
},
