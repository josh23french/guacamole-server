/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "client.h"
#include "channels/audio-input/audio-buffer.h"
#include "channels/cliprdr.h"
#include "channels/disp.h"
#include "common/recording.h"
#include "config.h"
#include "fs.h"
#include "log.h"
#include "rdp.h"
#include "settings.h"
#include "user.h"

#ifdef ENABLE_COMMON_SSH
#include "common-ssh/sftp.h"
#include "common-ssh/ssh.h"
#include "common-ssh/user.h"
#endif

#include <guacamole/audio.h>
#include <guacamole/client.h>

#include <pthread.h>
#include <stdlib.h>

int guac_client_init(guac_client* client, int argc, char** argv) {

    /* Set client args */
    client->args = GUAC_RDP_CLIENT_ARGS;

    /* Alloc client data */
    guac_rdp_client* rdp_client = calloc(1, sizeof(guac_rdp_client));
    client->data = rdp_client;

    /* Init clipboard */
    rdp_client->clipboard = guac_rdp_clipboard_alloc(client);

    /* Init display update module */
    rdp_client->disp = guac_rdp_disp_alloc();

    /* Redirect FreeRDP log messages to guac_client_log() */
    guac_rdp_redirect_wlog(client);

    /* Recursive attribute for locks */
    pthread_mutexattr_init(&(rdp_client->attributes));
    pthread_mutexattr_settype(&(rdp_client->attributes),
            PTHREAD_MUTEX_RECURSIVE);

    /* Set handlers */
    client->join_handler = guac_rdp_user_join_handler;
    client->free_handler = guac_rdp_client_free_handler;

#ifdef ENABLE_COMMON_SSH
    guac_common_ssh_init(client);
#endif

    return 0;

}

int guac_rdp_client_free_handler(guac_client* client) {

    guac_rdp_client* rdp_client = (guac_rdp_client*) client->data;

    /* Wait for client thread */
    pthread_join(rdp_client->client_thread, NULL);

    /* Free parsed settings */
    if (rdp_client->settings != NULL)
        guac_rdp_settings_free(rdp_client->settings);

    /* Clean up clipboard */
    guac_rdp_clipboard_free(rdp_client->clipboard);

    /* Free display update module */
    guac_rdp_disp_free(rdp_client->disp);

    /* Clean up filesystem, if allocated */
    if (rdp_client->filesystem != NULL)
        guac_rdp_fs_free(rdp_client->filesystem);

#ifdef ENABLE_COMMON_SSH
    /* Free SFTP filesystem, if loaded */
    if (rdp_client->sftp_filesystem)
        guac_common_ssh_destroy_sftp_filesystem(rdp_client->sftp_filesystem);

    /* Free SFTP session */
    if (rdp_client->sftp_session)
        guac_common_ssh_destroy_session(rdp_client->sftp_session);

    /* Free SFTP user */
    if (rdp_client->sftp_user)
        guac_common_ssh_destroy_user(rdp_client->sftp_user);

    guac_common_ssh_uninit();
#endif

    /* Clean up recording, if in progress */
    if (rdp_client->recording != NULL)
        guac_common_recording_free(rdp_client->recording);

    /* Clean up audio stream, if allocated */
    if (rdp_client->audio != NULL)
        guac_audio_stream_free(rdp_client->audio);

    /* Clean up audio input buffer, if allocated */
    if (rdp_client->audio_input != NULL)
        guac_rdp_audio_buffer_free(rdp_client->audio_input);

    /* Free client data */
    free(rdp_client);

    return 0;

}

