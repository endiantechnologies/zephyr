/* © 2020 Endian Technologies AB
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
                       LOG_ERR("Setting URAT ret:%d", ret);
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
           k_uptime_get() < 30*MSEC_PER_SEC) {
               /* Not enough time to get a good RSSI; try again */
               LOG_WRN("Waiting for a good RSSI value");
               return -1;
       }

       return minfo.mdm_service == 1 ? 0 : -1;
}
