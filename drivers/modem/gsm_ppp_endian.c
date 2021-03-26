/* © 2020, 2021 Endian Technologies AB
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

/* Various terrible hacks to the GSM PPP driver that are not ready to
 * be upstreamed. They've been used with the U-blox SARA R412M and
 * LTE-M.
 */

#if defined(CONFIG_MODEM_GSM_MNOPROF)
/* Handler: +UMNOPROF: <mnoprof> */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_mnoprof)
{
       size_t out_len;
       char buf[16];
       char *prof;

       out_len = net_buf_linearize(buf,
                                   sizeof(buf) - 1,
                                   data->rx_buf, 0, len);
       buf[out_len] = '\0';
       prof = strchr(buf, ':');
       if (!prof || *(prof+1) != ' ') {
               minfo.mdm_mnoprof = -1;
               return -1;
       }
       prof = prof + 2;
       minfo.mdm_mnoprof = atoi(prof);
       LOG_INF("MNO profile: %d", minfo.mdm_mnoprof);

       return 0;
}

/* Handler: +CPSMS: <mode>,[...] */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_psm)
{
       size_t out_len;
       char buf[16];
       char *psm;

       out_len = net_buf_linearize(buf,
                                   sizeof(buf) - 1,
                                   data->rx_buf, 0, len);
       buf[out_len] = '\0';

       psm = strchr(buf, ':');
       if (!psm) {
               return -1;
       }
       minfo.mdm_psm = *(psm+1) - '0';
       LOG_INF("PSM mode: %d", minfo.mdm_psm);

       return 0;
}

static int gsm_setup_mnoprof(struct gsm_modem *gsm)
{
       int ret;
       struct setup_cmd cmds[] = {
               SETUP_CMD_NOHANDLE("ATE0"),
               SETUP_CMD("AT+UMNOPROF?", "", on_cmd_atcmdinfo_mnoprof, 0U, ""),
       };

       ret = modem_cmd_handler_setup_cmds(&gsm->context.iface,
                                          &gsm->context.cmd_handler,
                                          cmds,
                                          ARRAY_SIZE(cmds),
                                          &gsm->sem_response,
                                          GSM_CMD_SETUP_TIMEOUT);
       if (ret < 0) {
               LOG_ERR("AT+UMNOPROF ret:%d", ret);
               return ret;
       }

       if (minfo.mdm_mnoprof != -1 && minfo.mdm_mnoprof != CONFIG_MODEM_GSM_MNOPROF) {
               /* The wrong MNO profile was set, change it */
               LOG_WRN("Changing MNO profile from %d to %d",
                       minfo.mdm_mnoprof, CONFIG_MODEM_GSM_MNOPROF);

               /* Detach from the network */
               ret = modem_cmd_send(&gsm->context.iface,
                                    &gsm->context.cmd_handler,
                                    NULL, 0,
                                    "AT+CFUN=0",
                                    &gsm->sem_response,
                                    GSM_CMD_AT_TIMEOUT);
               if (ret < 0) {
                       LOG_ERR("AT+CFUN=0 ret:%d", ret);
               }

               /* Set the profile */
               ret = modem_cmd_send(&gsm->context.iface,
                                    &gsm->context.cmd_handler,
                                    NULL, 0,
                                    "AT+UMNOPROF=" STRINGIFY(CONFIG_MODEM_GSM_MNOPROF),
                                    &gsm->sem_response,
                                    GSM_CMD_AT_TIMEOUT);
               if (ret < 0) {
                       LOG_ERR("AT+UMNOPROF ret:%d", ret);
               }

               /* Reboot */
               ret = modem_cmd_send(&gsm->context.iface,
                                    &gsm->context.cmd_handler,
                                    NULL, 0,
                                    "AT+CFUN=15",
                                    &gsm->sem_response,
                                    GSM_CMD_AT_TIMEOUT);
               if (ret < 0) {
                       LOG_ERR("AT+CFUN=15 ret:%d", ret);
               }
               k_sleep(K_SECONDS(3));

               return -EAGAIN;
       }

       return ret;
}

static int gsm_setup_psm(struct gsm_modem *gsm)
{
       int ret;
       struct setup_cmd query_cmds[] = {
               SETUP_CMD_NOHANDLE("ATE0"),
               SETUP_CMD("AT+CPSMS?", "", on_cmd_atcmdinfo_psm, 0U, ""),
       };
       struct setup_cmd set_cmds[] = {
               SETUP_CMD_NOHANDLE("ATE0"),
               SETUP_CMD_NOHANDLE("AT+CFUN=0"),
               SETUP_CMD_NOHANDLE("AT+CPSMS=0"),
               SETUP_CMD_NOHANDLE("AT+CFUN=15"),
       };

       ret = modem_cmd_handler_setup_cmds(&gsm->context.iface,
                                          &gsm->context.cmd_handler,
                                          query_cmds,
                                          ARRAY_SIZE(query_cmds),
                                          &gsm->sem_response,
                                          GSM_CMD_SETUP_TIMEOUT);
       if (ret < 0) {
               LOG_ERR("Querying PSM ret:%d", ret);
               return ret;
       }

       if (minfo.mdm_psm != -1 && minfo.mdm_psm != 0) {
               LOG_WRN("Disabling PSM");
               ret = modem_cmd_handler_setup_cmds(&gsm->context.iface,
                                                  &gsm->context.cmd_handler,
                                                  set_cmds,
                                                  ARRAY_SIZE(set_cmds),
                                                  &gsm->sem_response,
                                                  GSM_CMD_SETUP_TIMEOUT);
               if (ret < 0) {
                       LOG_ERR("Querying PSM ret:%d", ret);
                       return ret;
               }

               k_sleep(K_SECONDS(3));

               return -EAGAIN;
       }

       return ret;
}
#endif

#if defined(CONFIG_MODEM_GSM_URAT)
/* Handler: +URAT: <rat1>,[...] */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_urat)
{
       size_t out_len;

       out_len = net_buf_linearize(minfo.mdm_urat,
                                   sizeof(minfo.mdm_urat) - 1,
                                   data->rx_buf, 0, len);
       minfo.mdm_urat[out_len] = '\0';

       /* Get rid of "+URAT: " */
       char *p = strchr(minfo.mdm_urat, ' ');
       if (p) {
               size_t len = strlen(p+1);
               memmove(minfo.mdm_urat, p+1, len+1);
       }

       LOG_INF("URAT: %s", log_strdup(minfo.mdm_urat));

       return 0;
}

static int gsm_setup_urat(struct gsm_modem *gsm)
{
       int ret;
       struct setup_cmd query_cmds[] = {
               SETUP_CMD("AT+URAT?", "", on_cmd_atcmdinfo_urat, 0U, ""),
       };
       struct setup_cmd set_cmds[] = {
               SETUP_CMD_NOHANDLE("ATE0"),
               SETUP_CMD_NOHANDLE("AT+CFUN=0"),
               SETUP_CMD_NOHANDLE("AT+URAT=" CONFIG_MODEM_GSM_URAT),
               SETUP_CMD_NOHANDLE("AT+CFUN=15"),
       };

       ret = modem_cmd_handler_setup_cmds(&gsm->context.iface,
                                          &gsm->context.cmd_handler,
                                          query_cmds,
                                          ARRAY_SIZE(query_cmds),
                                          &gsm->sem_response,
                                          GSM_CMD_SETUP_TIMEOUT);
       if (ret < 0) {
               LOG_ERR("Querying URAT ret:%d", ret);
               return ret;
       }

       if (strcmp(minfo.mdm_urat, CONFIG_MODEM_GSM_URAT)) {
               LOG_WRN("Setting URAT");
               ret = modem_cmd_handler_setup_cmds(&gsm->context.iface,
                                                  &gsm->context.cmd_handler,
                                                  set_cmds,
                                                  ARRAY_SIZE(set_cmds),
                                                  &gsm->sem_response,
                                                  GSM_CMD_SETUP_TIMEOUT);
               if (ret < 0) {
                       LOG_ERR("Setting URAT ret:%d", ret);
                       return ret;
               }

               k_sleep(K_SECONDS(3));

               return -EAGAIN;
       }

       return ret;
}
#endif

#if defined(CONFIG_MODEM_GSM_CONFIGURE_UBANDMASK)
/* Handler: +UBANDMASK: <rat0>,<mask>,[...] */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_ubandmask)
{
       char buf[40];
       size_t out_len;

       out_len = net_buf_linearize(buf, sizeof(buf) - 1,
                                   data->rx_buf, 0, len);
       buf[out_len] = '\0';
       char *p = buf;

       /* Skip over "+UBANDMASK: " */
       if (strchr(buf, ' ')) {
               p = strchr(buf, ' ');
       }
       int i = 0;
       int rat = -1;
       while (p) {
               int v = atoi(p);

               if (i % 2 == 0) {
                       rat = v;
               } else if (rat >= 0 && rat < MDM_UBANDMASKS) {
                       minfo.mdm_bandmask[rat] = v;
                       LOG_INF("UBANDMASK for RAT %d: 0x%x", rat, v);
               }

               p = strchr(p, ',');
               if (p) p++;
               i++;
       }

       return 0;
}

static int gsm_setup_ubandmask(struct gsm_modem *gsm)
{
       int ret;
       struct setup_cmd query_cmds[] = {
               SETUP_CMD("AT+UBANDMASK?", "", on_cmd_atcmdinfo_ubandmask, 0U, ""),
       };
       struct setup_cmd set_cmds[] = {
               SETUP_CMD_NOHANDLE("ATE0"),
               SETUP_CMD_NOHANDLE("AT+CFUN=0"),
               SETUP_CMD_NOHANDLE("AT+UBANDMASK=0,"
                                  STRINGIFY(CONFIG_MODEM_GSM_UBANDMASK_M1)),
               SETUP_CMD_NOHANDLE("AT+UBANDMASK=1,"
                                  STRINGIFY(CONFIG_MODEM_GSM_UBANDMASK_NB1)),
               SETUP_CMD_NOHANDLE("AT+CFUN=15"),
       };

       ret = modem_cmd_handler_setup_cmds(&gsm->context.iface,
                                          &gsm->context.cmd_handler,
                                          query_cmds,
                                          ARRAY_SIZE(query_cmds),
                                          &gsm->sem_response,
                                          GSM_CMD_SETUP_TIMEOUT);
       if (ret < 0) {
               LOG_ERR("Querying UBANDMASK ret:%d", ret);
               return ret;
       }

       if (minfo.mdm_bandmask[0] != CONFIG_MODEM_GSM_UBANDMASK_M1 ||
           minfo.mdm_bandmask[1] != CONFIG_MODEM_GSM_UBANDMASK_NB1) {
               LOG_WRN("Setting UBANDMASK");
               ret = modem_cmd_handler_setup_cmds(&gsm->context.iface,
                                                  &gsm->context.cmd_handler,
                                                  set_cmds,
                                                  ARRAY_SIZE(set_cmds),
                                                  &gsm->sem_response,
                                                  GSM_CMD_SETUP_TIMEOUT);
               k_sleep(K_SECONDS(3));
               if (ret < 0) {
                       LOG_ERR("Setting UBANDMASK ret:%d", ret);
                       return ret;
               }


               return -EAGAIN;
       }

       return ret;
}
#endif

/* Handler: +CIND: <battchg>,<signal>,<service>,<sounder>,
 *          <message>,<call>,<roam>,<smsfull>,<gprs>,
 *          <callsetup>,<callheld>,<simind>
 */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_cind)
{
       char buf[40];
       size_t out_len;

       out_len = net_buf_linearize(buf, sizeof(buf) - 1,
                                   data->rx_buf, 0, len);
       buf[out_len] = '\0';

       char *p = buf;
       int i = 0;
       while (p) {
               int v = atoi(p);

               switch (i) {
               case 1:
                       switch (v) {
                       default:
                       case 0:
                               minfo.mdm_rssi = -106;
                               break;
                       case 1:
                               minfo.mdm_rssi = -92;
                               break;
                       case 2:
                               minfo.mdm_rssi = -82;
                               break;
                       case 3:
                               minfo.mdm_rssi = -70;
                               break;
                       case 4:
                               minfo.mdm_rssi = -58;
                               break;
                       case 5:
                               minfo.mdm_rssi = -57;
                               break;
                       }
                       if (v == 5) {
                               LOG_INF("RSSI: >=%d dBm", minfo.mdm_rssi);
                       } else {
                               LOG_INF("RSSI: <%d dBm", minfo.mdm_rssi+1);
                       }
                       break;
               case 2:
                       LOG_INF("Network service: %d", v);
                       minfo.mdm_service = v;
                       break;
               }

               p = strchr(p, ',');
               if (p) p++;
               i++;
       }

       return 0;
}

/* Poll the network status. Should return non-negative to indicate
 * that the network is ready to use.
 */
static int gsm_poll_network_status(struct gsm_modem *gsm)
{
       int ret;
       struct setup_cmd query_cmds[] = {
               SETUP_CMD("AT+CIND?", "", on_cmd_atcmdinfo_cind, 0U, ""),
       };

       ret = modem_cmd_handler_setup_cmds(&gsm->context.iface,
                                          &gsm->context.cmd_handler,
                                          query_cmds,
                                          ARRAY_SIZE(query_cmds),
                                          &gsm->sem_response,
                                          GSM_CMD_SETUP_TIMEOUT);
       if (ret < 0) {
               LOG_ERR("Querying CIND: %d", ret);
               return ret;
       }

       gsm->context.data_rssi = minfo.mdm_rssi;

       if (minfo.mdm_service == 1 &&
           gsm->context.data_rssi <= -106 &&
           (k_uptime_get() - minfo.mdm_setup_start) < 30*MSEC_PER_SEC) {
               /* Not enough time to get a good RSSI; try again */
               LOG_WRN("Waiting for a good RSSI value");
               return -1;
       }

       return minfo.mdm_service == 1 ? 0 : -1;
}

#if defined(CONFIG_MODEM_NETWORK_TIME)
/* Handler: +CTZU: <0/1/2> */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_ctzu)
{
	if (argc != 0) {
		minfo.mdm_use_nitz = atoi(argv[0]);
	}

	LOG_INF("CTZU: %d", minfo.mdm_use_nitz);

	return 0;
}

/* Configure the modem use use NITZ (network time & time zone). */
static int gsm_setup_nitz(struct gsm_modem *gsm)
{
	int ret;
	struct setup_cmd query_cmds[] = {
		SETUP_CMD("AT+CTZU?", "+CTZU:", on_cmd_atcmdinfo_ctzu, 1U, ","),
	};
	struct setup_cmd set_cmds[] = {
		SETUP_CMD_NOHANDLE("ATE0"),
		SETUP_CMD_NOHANDLE("AT+CTZU=1"),
		SETUP_CMD_NOHANDLE("AT+CFUN=15"),
	};

	ret = modem_cmd_handler_setup_cmds(&gsm->context.iface,
					   &gsm->context.cmd_handler,
					   query_cmds,
					   ARRAY_SIZE(query_cmds),
					   &gsm->sem_response,
					   GSM_CMD_SETUP_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("Querying CTZU ret:%d", ret);
		return ret;
	}

	if (minfo.mdm_use_nitz != 1) {
		LOG_WRN("Enabling NITZ");
		ret = modem_cmd_handler_setup_cmds(&gsm->context.iface,
						   &gsm->context.cmd_handler,
						   set_cmds,
						   ARRAY_SIZE(set_cmds),
						   &gsm->sem_response,
						   GSM_CMD_SETUP_TIMEOUT);
		if (ret < 0) {
			LOG_ERR("Setting CTZU ret:%d", ret);
			return ret;
		}

		k_sleep(K_SECONDS(3));

		return -EAGAIN;
	}

	return ret;
}

/* Handler: +CCLK: "yy/MM/dd,hh:mm:ss+TZ"
 * TZ is expressed in multiples of 15 minutes.
 */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_cclk)
{
	char buf[40];
	size_t out_len;

	out_len = net_buf_linearize(buf, sizeof(buf) - 1,
				    data->rx_buf, 0, len);
	buf[out_len] = '\0';
	char *p;

	/* Skip over '+CCLK: "' */
	if ((p = strchr(buf, '"'))) {
		p++;
	} else {
		p = buf;
	}

	/* Delim is set to the character that delimited the previous
	 * field (e.g. in decoding "21/" it is set to '/').
	 */
	char delim = ' ';
	for (int i = 0; *p; i++) {
		char *endp = NULL;
		int v = strtoul(p, &endp, 10);

		if (p == endp) {
			break;
		}

		switch (i) {
		case 0:
			minfo.mdm_nitz.tm_year = 100+v;
			break;
		case 1:
			minfo.mdm_nitz.tm_mon = v-1;
			break;
		case 2:
			minfo.mdm_nitz.tm_mday = v;
			break;
		case 3:
			minfo.mdm_nitz.tm_hour = v;
			break;
		case 4:
			minfo.mdm_nitz.tm_min = v;
			break;
		case 5:
			minfo.mdm_nitz.tm_sec = v;
			break;
		case 6:
			minfo.mdm_tzoffset = v * 15;
			if (delim == '-') {
				minfo.mdm_tzoffset = -minfo.mdm_tzoffset;
			}
			break;
		}

		delim = *endp;
		p = endp+1;
	}

	minfo.mdm_nitz_uptime = k_uptime_get();

	return 0;
}

/* Read the network time. */
static int gsm_read_network_time(struct gsm_modem *gsm)
{
	int ret;
	struct setup_cmd query_cmds[] = {
		SETUP_CMD_NOHANDLE("AT+CTZU?"),
		SETUP_CMD("AT+CCLK?", "", on_cmd_atcmdinfo_cclk, 0U, ""),
	};

	ret = modem_cmd_handler_setup_cmds(&gsm->context.iface,
					   &gsm->context.cmd_handler,
					   query_cmds,
					   ARRAY_SIZE(query_cmds),
					   &gsm->sem_response,
					   GSM_CMD_SETUP_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("Querying CCLK: %d", ret);
		return ret;
	}

	LOG_DBG("Network time: %04d-%02d-%02d %02d:%02d:%02d UTC%c%02d%02d",
		1900+minfo.mdm_nitz.tm_year,
		1+minfo.mdm_nitz.tm_mon,
		minfo.mdm_nitz.tm_mday,
		minfo.mdm_nitz.tm_hour,
		minfo.mdm_nitz.tm_min,
		minfo.mdm_nitz.tm_sec,
		minfo.mdm_tzoffset >= 0 ? '+' : '-',
		minfo.mdm_tzoffset/60,
		minfo.mdm_tzoffset%60);

	int year = 1900+minfo.mdm_nitz.tm_year;

	if (year == 2080 || year < 2021) {
		/* The time is obviously invalid. SARA R412M initially
		 * reports 2080 until it gets the network time.
		 */
		return -1;
	}

	gsm->context.data_tz_minutes = minfo.mdm_tzoffset;
	gsm->context.data_time = minfo.mdm_nitz;
	gsm->context.data_time_uptime = minfo.mdm_nitz_uptime;

	return 0;
}
#endif	/* CONFIG_MODEM_NETWORK_TIME */

#if defined(CONFIG_MODEM_OPERATOR_INFO)
/* Handler: +COPS: <mode>[,<format>,<oper>[,<AcT>]] */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_cops)
{
	if (argc >= 4) {
		minfo.mdm_rat = atoi(argv[3]);
	}

	return 0;
}

static int gsm_read_operator_information(struct gsm_modem *gsm)
{
	int ret;
	struct setup_cmd query_cmds[] = {
		SETUP_CMD("AT+COPS?", "", on_cmd_atcmdinfo_cops, 4U, ","),
	};

	ret = modem_cmd_handler_setup_cmds(&gsm->context.iface,
					   &gsm->context.cmd_handler,
					   query_cmds,
					   ARRAY_SIZE(query_cmds),
					   &gsm->sem_response,
					   GSM_CMD_SETUP_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("Querying COPS: %d", ret);
		return ret;
	}

	gsm->context.data_rat = minfo.mdm_rat;

	return 0;
}
#endif
