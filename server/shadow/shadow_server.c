/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 *
 * Copyright 2014 Marc-Andre Moreau <marcandre.moreau@gmail.com>
 * Copyright 2017 Armin Novak <armin.novak@thincast.com>
 * Copyright 2017 Thincast Technologies GmbH
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <freerdp/config.h>

#include <errno.h>
#include <stdint.h>

#include <winpr/assert.h>
#include <winpr/crt.h>
#include <winpr/ssl.h>
#include <winpr/path.h>
#include <winpr/cmdline.h>
#include <winpr/winsock.h>

#include <freerdp/log.h>
#include <freerdp/version.h>

#include <winpr/tools/makecert.h>

#ifndef _WIN32
#include <sys/select.h>
#include <signal.h>
#endif

#include "shadow.h"

#define TAG SERVER_TAG("shadow")

static const char bind_address[] = "bind-address,";

#define fail_at(arg, rc) fail_at_((arg), (rc), __FILE__, __func__, __LINE__)
static int fail_at_(const COMMAND_LINE_ARGUMENT_A* arg, int rc, const char* file, const char* fkt,
                    size_t line)
{
	const DWORD level = WLOG_ERROR;
	wLog* log = WLog_Get(TAG);
	if (WLog_IsLevelActive(log, level))
		WLog_PrintMessage(log, WLOG_MESSAGE_TEXT, level, line, file, fkt,
		                  "Command line parsing failed at '%s' value '%s' [%d]", arg->Name,
		                  arg->Value, rc);
	return rc;
}

static int command_line_compare(const void* pa, const void* pb)
{
	const COMMAND_LINE_ARGUMENT_A* a = pa;
	const COMMAND_LINE_ARGUMENT_A* b = pb;

	if (!a && !b)
		return 0;
	if (!a)
		return -1;
	if (!b)
		return 1;

	return strcmp(a->Name, b->Name);
}

static int shadow_server_print_command_line_help(int argc, char** argv,
                                                 const COMMAND_LINE_ARGUMENT_A* largs)
{
	if ((argc < 1) || !largs || !argv)
		return -1;

	{
		char* path = winpr_GetConfigFilePath(TRUE, "SAM");
		printf("Usage: %s [options]\n", argv[0]);
		printf("\n");
		printf("Notes: By default NLA security is active.\n");
		printf("\tIn this mode a SAM database is required.\n");
		printf("\tProvide one with /sam-file:<file with path>\n");
		printf("\telse the default path %s is used.\n", path);
		printf("\tIf there is no existing SAM file authentication for all users will fail.\n");
		printf("\n\tIf authentication against PAM is desired, start with -sec-nla (requires "
		       "compiled in "
		       "support for PAM)\n\n");
		printf("Syntax:\n");
		printf("    /flag (enables flag)\n");
		printf("    /option:<value> (specifies option with value)\n");
		printf("    +toggle -toggle (enables or disables toggle, where '/' is a synonym of '+')\n");
		printf("\n");
		free(path);
	}

	// TODO: Sort arguments
	size_t nrArgs = 0;
	{
		const COMMAND_LINE_ARGUMENT_A* arg = largs;
		while (arg->Name != NULL)
		{
			nrArgs++;
			arg++;
		}
		nrArgs++;
	}
	COMMAND_LINE_ARGUMENT_A* args_copy = calloc(nrArgs, sizeof(COMMAND_LINE_ARGUMENT_A));
	if (!args_copy)
		return -1;
	memcpy(args_copy, largs, nrArgs * sizeof(COMMAND_LINE_ARGUMENT_A));
	qsort(args_copy, nrArgs - 1, sizeof(COMMAND_LINE_ARGUMENT_A), command_line_compare);

	const COMMAND_LINE_ARGUMENT_A* arg = args_copy;

	int rc = -1;
	do
	{
		if (arg->Flags & COMMAND_LINE_VALUE_FLAG)
		{
			printf("    %s", "/");
			printf("%-20s\n", arg->Name);
			printf("\t%s\n", arg->Text);
		}
		else if ((arg->Flags & COMMAND_LINE_VALUE_REQUIRED) ||
		         (arg->Flags & COMMAND_LINE_VALUE_OPTIONAL))
		{
			printf("    %s", "/");

			if (arg->Format)
			{
				const size_t length = (strlen(arg->Name) + strlen(arg->Format) + 2);
				char* str = (char*)calloc(length + 1, sizeof(char));

				if (!str)
					goto fail;

				(void)sprintf_s(str, length + 1, "%s:%s", arg->Name, arg->Format);
				(void)printf("%-20s\n", str);
				free(str);
			}
			else
			{
				printf("%-20s\n", arg->Name);
			}

			printf("\t%s\n", arg->Text);
		}
		else if (arg->Flags & COMMAND_LINE_VALUE_BOOL)
		{
			const size_t length = strlen(arg->Name) + 32;
			char* str = calloc(length + 1, sizeof(char));

			if (!str)
				goto fail;

			(void)sprintf_s(str, length + 1, "%s (default:%s)", arg->Name,
			                arg->Default ? "on" : "off");
			(void)printf("    %s", arg->Default ? "-" : "+");
			(void)printf("%-20s\n", str);
			(void)printf("\t%s\n", arg->Text);

			free(str);
		}
	} while ((arg = CommandLineFindNextArgumentA(arg)) != NULL);

	rc = 1;
fail:
	free(args_copy);
	return rc;
}

int shadow_server_command_line_status_print(rdpShadowServer* server, int argc, char** argv,
                                            int status, const COMMAND_LINE_ARGUMENT_A* cargs)
{
	WINPR_UNUSED(server);

	if (status == COMMAND_LINE_STATUS_PRINT_VERSION)
	{
		printf("FreeRDP version %s (git %s)\n", FREERDP_VERSION_FULL, FREERDP_GIT_REVISION);
		return COMMAND_LINE_STATUS_PRINT_VERSION;
	}
	else if (status == COMMAND_LINE_STATUS_PRINT_BUILDCONFIG)
	{
		printf("%s\n", freerdp_get_build_config());
		return COMMAND_LINE_STATUS_PRINT_BUILDCONFIG;
	}
	else if (status == COMMAND_LINE_STATUS_PRINT)
	{
		return COMMAND_LINE_STATUS_PRINT;
	}
	else if (status < 0)
	{
		if (shadow_server_print_command_line_help(argc, argv, cargs) < 0)
			return -1;

		return COMMAND_LINE_STATUS_PRINT_HELP;
	}

	return 1;
}

int shadow_server_parse_command_line(rdpShadowServer* server, int argc, char** argv,
                                     COMMAND_LINE_ARGUMENT_A* cargs)
{
	int status = 0;
	DWORD flags = 0;
	const COMMAND_LINE_ARGUMENT_A* arg = NULL;
	rdpSettings* settings = server->settings;

	if ((argc < 2) || !argv || !cargs)
		return 1;

	CommandLineClearArgumentsA(cargs);
	flags = COMMAND_LINE_SEPARATOR_COLON;
	flags |= COMMAND_LINE_SIGIL_SLASH | COMMAND_LINE_SIGIL_PLUS_MINUS;
	status = CommandLineParseArgumentsA(argc, argv, cargs, flags, server, NULL, NULL);

	if (status < 0)
		return status;

	arg = cargs;
	errno = 0;

	do
	{
		if (!(arg->Flags & COMMAND_LINE_ARGUMENT_PRESENT))
			continue;

		CommandLineSwitchStart(arg) CommandLineSwitchCase(arg, "port")
		{
			long val = strtol(arg->Value, NULL, 0);

			if ((errno != 0) || (val <= 0) || (val > UINT16_MAX))
				return fail_at(arg, COMMAND_LINE_ERROR);

			server->port = (DWORD)val;
		}
		CommandLineSwitchCase(arg, "ipc-socket")
		{
			/* /bind-address is incompatible */
			if (server->ipcSocket)
				return fail_at(arg, COMMAND_LINE_ERROR);
			server->ipcSocket = _strdup(arg->Value);

			if (!server->ipcSocket)
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "bind-address")
		{
			int rc = 0;
			size_t len = strlen(arg->Value) + sizeof(bind_address);
			/* /ipc-socket is incompatible */
			if (server->ipcSocket)
				return fail_at(arg, COMMAND_LINE_ERROR);
			server->ipcSocket = calloc(len, sizeof(CHAR));

			if (!server->ipcSocket)
				return fail_at(arg, COMMAND_LINE_ERROR);

			rc = _snprintf(server->ipcSocket, len, "%s%s", bind_address, arg->Value);
			if ((rc < 0) || ((size_t)rc != len - 1))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "may-view")
		{
			server->mayView = arg->Value ? TRUE : FALSE;
		}
		CommandLineSwitchCase(arg, "bitmap-compat")
		{
			server->SupportMultiRectBitmapUpdates = arg->Value ? FALSE : TRUE;
		}
		CommandLineSwitchCase(arg, "may-interact")
		{
			server->mayInteract = arg->Value ? TRUE : FALSE;
		}
		CommandLineSwitchCase(arg, "server-side-cursor")
		{
			server->ShowMouseCursor = arg->Value ? TRUE : FALSE;
		}
		CommandLineSwitchCase(arg, "mouse-relative")
		{
			const BOOL val = arg->Value ? TRUE : FALSE;
			if (!freerdp_settings_set_bool(settings, FreeRDP_MouseUseRelativeMove, val) ||
			    !freerdp_settings_set_bool(settings, FreeRDP_HasRelativeMouseEvent, val))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "max-connections")
		{
			errno = 0;
			unsigned long val = strtoul(arg->Value, NULL, 0);

			if ((errno != 0) || (val > UINT32_MAX))
				return fail_at(arg, COMMAND_LINE_ERROR);
			server->maxClientsConnected = val;
		}
		CommandLineSwitchCase(arg, "rect")
		{
			char* p = NULL;
			char* tok[4];
			long x = -1;
			long y = -1;
			long w = -1;
			long h = -1;
			char* str = _strdup(arg->Value);

			if (!str)
				return fail_at(arg, COMMAND_LINE_ERROR);

			tok[0] = p = str;
			p = strchr(p + 1, ',');

			if (!p)
			{
				free(str);
				return fail_at(arg, COMMAND_LINE_ERROR);
			}

			*p++ = '\0';
			tok[1] = p;
			p = strchr(p + 1, ',');

			if (!p)
			{
				free(str);
				return fail_at(arg, COMMAND_LINE_ERROR);
			}

			*p++ = '\0';
			tok[2] = p;
			p = strchr(p + 1, ',');

			if (!p)
			{
				free(str);
				return fail_at(arg, COMMAND_LINE_ERROR);
			}

			*p++ = '\0';
			tok[3] = p;
			x = strtol(tok[0], NULL, 0);

			if (errno != 0)
				goto fail;

			y = strtol(tok[1], NULL, 0);

			if (errno != 0)
				goto fail;

			w = strtol(tok[2], NULL, 0);

			if (errno != 0)
				goto fail;

			h = strtol(tok[3], NULL, 0);

			if (errno != 0)
				goto fail;

		fail:
			free(str);

			if ((x < 0) || (y < 0) || (w < 1) || (h < 1) || (errno != 0))
				return fail_at(arg, COMMAND_LINE_ERROR);

			if ((x > UINT16_MAX) || (y > UINT16_MAX) || (x + w > UINT16_MAX) ||
			    (y + h > UINT16_MAX))
				return fail_at(arg, COMMAND_LINE_ERROR);
			server->subRect.left = (UINT16)x;
			server->subRect.top = (UINT16)y;
			server->subRect.right = (UINT16)(x + w);
			server->subRect.bottom = (UINT16)(y + h);
			server->shareSubRect = TRUE;
		}
		CommandLineSwitchCase(arg, "auth")
		{
			server->authentication = arg->Value ? TRUE : FALSE;
		}
		CommandLineSwitchCase(arg, "remote-guard")
		{
			if (!freerdp_settings_set_bool(settings, FreeRDP_RemoteCredentialGuard,
			                               arg->Value ? TRUE : FALSE))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "restricted-admin")
		{
			if (!freerdp_settings_set_bool(settings, FreeRDP_RestrictedAdminModeSupported,
			                               arg->Value ? TRUE : FALSE))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "vmconnect")
		{
			if (!freerdp_settings_set_bool(settings, FreeRDP_VmConnectMode,
			                               arg->Value ? TRUE : FALSE))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "sec")
		{
			if (strcmp("rdp", arg->Value) == 0) /* Standard RDP */
			{
				if (!freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, TRUE))
					return fail_at(arg, COMMAND_LINE_ERROR);
				if (!freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, FALSE))
					return fail_at(arg, COMMAND_LINE_ERROR);
				if (!freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, FALSE))
					return fail_at(arg, COMMAND_LINE_ERROR);
				if (!freerdp_settings_set_bool(settings, FreeRDP_ExtSecurity, FALSE))
					return fail_at(arg, COMMAND_LINE_ERROR);
				if (!freerdp_settings_set_bool(settings, FreeRDP_UseRdpSecurityLayer, TRUE))
					return fail_at(arg, COMMAND_LINE_ERROR);
			}
			else if (strcmp("tls", arg->Value) == 0) /* TLS */
			{
				if (!freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, FALSE))
					return fail_at(arg, COMMAND_LINE_ERROR);
				if (!freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, TRUE))
					return fail_at(arg, COMMAND_LINE_ERROR);
				if (!freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, FALSE))
					return fail_at(arg, COMMAND_LINE_ERROR);
				if (!freerdp_settings_set_bool(settings, FreeRDP_ExtSecurity, FALSE))
					return fail_at(arg, COMMAND_LINE_ERROR);
			}
			else if (strcmp("nla", arg->Value) == 0) /* NLA */
			{
				if (!freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, FALSE))
					return fail_at(arg, COMMAND_LINE_ERROR);
				if (!freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, FALSE))
					return fail_at(arg, COMMAND_LINE_ERROR);
				if (!freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, TRUE))
					return fail_at(arg, COMMAND_LINE_ERROR);
				if (!freerdp_settings_set_bool(settings, FreeRDP_ExtSecurity, FALSE))
					return fail_at(arg, COMMAND_LINE_ERROR);
			}
			else if (strcmp("ext", arg->Value) == 0) /* NLA Extended */
			{
				if (!freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, FALSE))
					return fail_at(arg, COMMAND_LINE_ERROR);
				if (!freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, FALSE))
					return fail_at(arg, COMMAND_LINE_ERROR);
				if (!freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, FALSE))
					return fail_at(arg, COMMAND_LINE_ERROR);
				if (!freerdp_settings_set_bool(settings, FreeRDP_ExtSecurity, TRUE))
					return fail_at(arg, COMMAND_LINE_ERROR);
			}
			else
			{
				WLog_ERR(TAG, "unknown protocol security: %s", arg->Value);
				return fail_at(arg, COMMAND_LINE_ERROR_UNEXPECTED_VALUE);
			}
		}
		CommandLineSwitchCase(arg, "sec-rdp")
		{
			if (!freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity,
			                               arg->Value ? TRUE : FALSE))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "sec-tls")
		{
			if (!freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity,
			                               arg->Value ? TRUE : FALSE))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "sec-nla")
		{
			if (!freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity,
			                               arg->Value ? TRUE : FALSE))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "sec-ext")
		{
			if (!freerdp_settings_set_bool(settings, FreeRDP_ExtSecurity,
			                               arg->Value ? TRUE : FALSE))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "sam-file")
		{
			if (!freerdp_settings_set_string(settings, FreeRDP_NtlmSamFile, arg->Value))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "log-level")
		{
			wLog* root = WLog_GetRoot();

			if (!WLog_SetStringLogLevel(root, arg->Value))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "log-filters")
		{
			if (!WLog_AddStringLogFilters(arg->Value))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "nsc")
		{
			if (!freerdp_settings_set_bool(settings, FreeRDP_NSCodec, arg->Value ? TRUE : FALSE))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "rfx")
		{
			if (!freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec,
			                               arg->Value ? TRUE : FALSE))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "gfx")
		{
			if (!freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline,
			                               arg->Value ? TRUE : FALSE))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "gfx-progressive")
		{
			if (!freerdp_settings_set_bool(settings, FreeRDP_GfxProgressive,
			                               arg->Value ? TRUE : FALSE))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "gfx-rfx")
		{
			if (!freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec,
			                               arg->Value ? TRUE : FALSE))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "gfx-planar")
		{
			if (!freerdp_settings_set_bool(settings, FreeRDP_GfxPlanar, arg->Value ? TRUE : FALSE))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "gfx-avc420")
		{
			if (!freerdp_settings_set_bool(settings, FreeRDP_GfxH264, arg->Value ? TRUE : FALSE))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "gfx-avc444")
		{
			if (!freerdp_settings_set_bool(settings, FreeRDP_GfxAVC444v2,
			                               arg->Value ? TRUE : FALSE))
				return fail_at(arg, COMMAND_LINE_ERROR);
			if (!freerdp_settings_set_bool(settings, FreeRDP_GfxAVC444, arg->Value ? TRUE : FALSE))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "keytab")
		{
			if (!freerdp_settings_set_string(settings, FreeRDP_KerberosKeytab, arg->Value))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "ccache")
		{
			if (!freerdp_settings_set_string(settings, FreeRDP_KerberosCache, arg->Value))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchCase(arg, "tls-secrets-file")
		{
			if (!freerdp_settings_set_string(settings, FreeRDP_TlsSecretsFile, arg->Value))
				return fail_at(arg, COMMAND_LINE_ERROR);
		}
		CommandLineSwitchDefault(arg)
		{
		}
		CommandLineSwitchEnd(arg)
	} while ((arg = CommandLineFindNextArgumentA(arg)) != NULL);

	arg = CommandLineFindArgumentA(cargs, "monitors");

	if (arg && (arg->Flags & COMMAND_LINE_ARGUMENT_PRESENT))
	{
		UINT32 numMonitors = 0;
		MONITOR_DEF monitors[16] = { 0 };
		numMonitors = shadow_enum_monitors(monitors, 16);

		if (arg->Flags & COMMAND_LINE_VALUE_PRESENT)
		{
			/* Select monitors */
			long val = strtol(arg->Value, NULL, 0);

			if ((val < 0) || (errno != 0) || ((UINT32)val >= numMonitors))
				status = COMMAND_LINE_STATUS_PRINT;

			server->selectedMonitor = (UINT32)val;
		}
		else
		{
			/* List monitors */

			for (UINT32 index = 0; index < numMonitors; index++)
			{
				const MONITOR_DEF* monitor = &monitors[index];
				const INT64 width = monitor->right - monitor->left + 1;
				const INT64 height = monitor->bottom - monitor->top + 1;
				WLog_INFO(TAG, "      %s [%d] %" PRId64 "x%" PRId64 "\t+%" PRId32 "+%" PRId32 "",
				          (monitor->flags == 1) ? "*" : " ", index, width, height, monitor->left,
				          monitor->top);
			}

			status = COMMAND_LINE_STATUS_PRINT;
		}
	}

	/* If we want to disable authentication we need to ensure that NLA security
	 * is not activated. Only TLS and RDP security allow anonymous login.
	 */
	if (!server->authentication && !freerdp_settings_get_bool(settings, FreeRDP_VmConnectMode))
	{
		if (!freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, FALSE))
			return COMMAND_LINE_ERROR;
	}
	return status;
}

static DWORD WINAPI shadow_server_thread(LPVOID arg)
{
	rdpShadowServer* server = (rdpShadowServer*)arg;
	BOOL running = TRUE;
	DWORD status = 0;
	freerdp_listener* listener = server->listener;
	shadow_subsystem_start(server->subsystem);

	while (running)
	{
		HANDLE events[MAXIMUM_WAIT_OBJECTS] = { 0 };
		DWORD nCount = 0;
		events[nCount++] = server->StopEvent;
		nCount += listener->GetEventHandles(listener, &events[nCount], ARRAYSIZE(events) - nCount);

		if (nCount <= 1)
		{
			WLog_ERR(TAG, "Failed to get FreeRDP file descriptor");
			break;
		}

		status = WaitForMultipleObjects(nCount, events, FALSE, INFINITE);

		switch (status)
		{
			case WAIT_FAILED:
			case WAIT_OBJECT_0:
				running = FALSE;
				break;

			default:
			{
				if (!listener->CheckFileDescriptor(listener))
				{
					WLog_ERR(TAG, "Failed to check FreeRDP file descriptor");
					running = FALSE;
				}
				else
				{
#ifdef _WIN32
					Sleep(100); /* FIXME: listener event handles */
#endif
				}
			}
			break;
		}
	}

	listener->Close(listener);
	shadow_subsystem_stop(server->subsystem);

	/* Signal to the clients that server is being stopped and wait for them
	 * to disconnect. */
	if (shadow_client_boardcast_quit(server, 0))
	{
		while (ArrayList_Count(server->clients) > 0)
		{
			Sleep(100);
		}
	}

	ExitThread(0);
	return 0;
}

static BOOL open_port(rdpShadowServer* server, char* address)
{
	BOOL status = 0;
	char* modaddr = address;

	if (modaddr)
	{
		if (modaddr[0] == '[')
		{
			char* end = strchr(address, ']');
			if (!end)
			{
				WLog_ERR(TAG, "Could not parse bind-address %s", address);
				return -1;
			}
			*end++ = '\0';
			if (strlen(end) > 0)
			{
				WLog_ERR(TAG, "Excess data after IPv6 address: '%s'", end);
				return -1;
			}
			modaddr++;
		}
	}
	status = server->listener->Open(server->listener, modaddr, (UINT16)server->port);

	if (!status)
	{
		WLog_ERR(TAG,
		         "Problem creating TCP listener. (Port already used or insufficient permissions?)");
	}

	return status;
}

int shadow_server_start(rdpShadowServer* server)
{
	BOOL ipc = 0;
	BOOL status = 0;
	WSADATA wsaData;

	if (!server)
		return -1;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		return -1;

#ifndef _WIN32
	(void)signal(SIGPIPE, SIG_IGN);
#endif
	server->screen = shadow_screen_new(server);

	if (!server->screen)
	{
		WLog_ERR(TAG, "screen_new failed");
		return -1;
	}

	server->capture = shadow_capture_new(server);

	if (!server->capture)
	{
		WLog_ERR(TAG, "capture_new failed");
		return -1;
	}

	/* Bind magic:
	 *
	 * empty                 ... bind TCP all
	 * <local path>          ... bind local (IPC)
	 * bind-socket,<address> ... bind TCP to specified interface
	 */
	ipc = server->ipcSocket && (strncmp(bind_address, server->ipcSocket,
	                                    strnlen(bind_address, sizeof(bind_address))) != 0);
	if (!ipc)
	{
		size_t count = 0;

		char** ptr = CommandLineParseCommaSeparatedValuesEx(NULL, server->ipcSocket, &count);
		if (!ptr || (count <= 1))
		{
			if (server->ipcSocket == NULL)
			{
				if (!open_port(server, NULL))
				{
					CommandLineParserFree(ptr);
					return -1;
				}
			}
			else
			{
				CommandLineParserFree(ptr);
				return -1;
			}
		}

		WINPR_ASSERT(ptr || (count == 0));
		for (size_t x = 1; x < count; x++)
		{
			BOOL success = open_port(server, ptr[x]);
			if (!success)
			{
				CommandLineParserFree(ptr);
				return -1;
			}
		}
		CommandLineParserFree(ptr);
	}
	else
	{
		status = server->listener->OpenLocal(server->listener, server->ipcSocket);

		if (!status)
		{
			WLog_ERR(TAG, "Problem creating local socket listener. (Port already used or "
			              "insufficient permissions?)");
			return -1;
		}
	}

	if (!(server->thread = CreateThread(NULL, 0, shadow_server_thread, (void*)server, 0, NULL)))
	{
		return -1;
	}

	return 0;
}

int shadow_server_stop(rdpShadowServer* server)
{
	if (!server)
		return -1;

	if (server->thread)
	{
		(void)SetEvent(server->StopEvent);
		(void)WaitForSingleObject(server->thread, INFINITE);
		(void)CloseHandle(server->thread);
		server->thread = NULL;
		if (server->listener && server->listener->Close)
			server->listener->Close(server->listener);
	}

	if (server->screen)
	{
		shadow_screen_free(server->screen);
		server->screen = NULL;
	}

	if (server->capture)
	{
		shadow_capture_free(server->capture);
		server->capture = NULL;
	}

	return 0;
}

static int shadow_server_init_config_path(rdpShadowServer* server)
{
	if (!server->ConfigPath)
	{
		char* configHome = freerdp_settings_get_config_path();

		if (configHome)
		{
			if (!winpr_PathFileExists(configHome) && !winpr_PathMakePath(configHome, 0))
			{
				WLog_ERR(TAG, "Failed to create directory '%s'", configHome);
				free(configHome);
				return -1;
			}

			server->ConfigPath = configHome;
		}
	}

	if (!server->ConfigPath)
		return -1; /* no usable config path */

	return 1;
}

static BOOL shadow_server_create_certificate(rdpShadowServer* server, const char* filepath)
{
	BOOL rc = FALSE;
	char* makecert_argv[6] = { "makecert", "-rdp", "-live", "-silent", "-y", "5" };

	WINPR_STATIC_ASSERT(ARRAYSIZE(makecert_argv) <= INT_MAX);
	const size_t makecert_argc = ARRAYSIZE(makecert_argv);

	MAKECERT_CONTEXT* makecert = makecert_context_new();

	if (!makecert)
		goto out_fail;

	if (makecert_context_process(makecert, (int)makecert_argc, makecert_argv) < 0)
		goto out_fail;

	if (makecert_context_set_output_file_name(makecert, "shadow") != 1)
		goto out_fail;

	WINPR_ASSERT(server);
	WINPR_ASSERT(filepath);
	if (!winpr_PathFileExists(server->CertificateFile))
	{
		if (makecert_context_output_certificate_file(makecert, filepath) != 1)
			goto out_fail;
	}

	if (!winpr_PathFileExists(server->PrivateKeyFile))
	{
		if (makecert_context_output_private_key_file(makecert, filepath) != 1)
			goto out_fail;
	}
	rc = TRUE;
out_fail:
	makecert_context_free(makecert);
	return rc;
}
static BOOL shadow_server_init_certificate(rdpShadowServer* server)
{
	char* filepath = NULL;
	BOOL ret = FALSE;

	WINPR_ASSERT(server);

	if (!winpr_PathFileExists(server->ConfigPath) && !winpr_PathMakePath(server->ConfigPath, 0))
	{
		WLog_ERR(TAG, "Failed to create directory '%s'", server->ConfigPath);
		return FALSE;
	}

	if (!(filepath = GetCombinedPath(server->ConfigPath, "shadow")))
		return FALSE;

	if (!winpr_PathFileExists(filepath) && !winpr_PathMakePath(filepath, 0))
	{
		if (!winpr_PathMakePath(filepath, NULL))
		{
			WLog_ERR(TAG, "Failed to create directory '%s'", filepath);
			goto out_fail;
		}
	}

	server->CertificateFile = GetCombinedPath(filepath, "shadow.crt");
	server->PrivateKeyFile = GetCombinedPath(filepath, "shadow.key");

	if (!server->CertificateFile || !server->PrivateKeyFile)
		goto out_fail;

	if ((!winpr_PathFileExists(server->CertificateFile)) ||
	    (!winpr_PathFileExists(server->PrivateKeyFile)))
	{
		if (!shadow_server_create_certificate(server, filepath))
			goto out_fail;
	}

	rdpSettings* settings = server->settings;
	WINPR_ASSERT(settings);

	rdpPrivateKey* key = freerdp_key_new_from_file_enc(server->PrivateKeyFile, NULL);
	if (!key)
		goto out_fail;
	if (!freerdp_settings_set_pointer_len(settings, FreeRDP_RdpServerRsaKey, key, 1))
		goto out_fail;

	rdpCertificate* cert = freerdp_certificate_new_from_file(server->CertificateFile);
	if (!cert)
		goto out_fail;

	if (!freerdp_settings_set_pointer_len(settings, FreeRDP_RdpServerCertificate, cert, 1))
		goto out_fail;

	if (!freerdp_certificate_is_rdp_security_compatible(cert))
	{
		if (!freerdp_settings_set_bool(settings, FreeRDP_UseRdpSecurityLayer, FALSE))
			goto out_fail;
		if (!freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, FALSE))
			goto out_fail;
	}
	ret = TRUE;
out_fail:
	free(filepath);
	return ret;
}

static BOOL shadow_server_check_peer_restrictions(freerdp_listener* listener)
{
	WINPR_ASSERT(listener);

	rdpShadowServer* server = (rdpShadowServer*)listener->info;
	WINPR_ASSERT(server);

	if (server->maxClientsConnected > 0)
	{
		const size_t count = ArrayList_Count(server->clients);
		if (count >= server->maxClientsConnected)
		{
			WLog_WARN(TAG, "connection limit [%" PRIuz "] reached, discarding client",
			          server->maxClientsConnected);
			return FALSE;
		}
	}
	return TRUE;
}

int shadow_server_init(rdpShadowServer* server)
{
	int status = 0;
	winpr_InitializeSSL(WINPR_SSL_INIT_DEFAULT);
	WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi());

	if (!(server->clients = ArrayList_New(TRUE)))
		goto fail;

	if (!(server->StopEvent = CreateEvent(NULL, TRUE, FALSE, NULL)))
		goto fail;

	if (!InitializeCriticalSectionAndSpinCount(&(server->lock), 4000))
		goto fail;

	status = shadow_server_init_config_path(server);

	if (status < 0)
		goto fail;

	if (!shadow_server_init_certificate(server))
		goto fail;

	server->listener = freerdp_listener_new();

	if (!server->listener)
		goto fail;

	server->listener->info = (void*)server;
	server->listener->CheckPeerAcceptRestrictions = shadow_server_check_peer_restrictions;
	server->listener->PeerAccepted = shadow_client_accepted;
	server->subsystem = shadow_subsystem_new();

	if (!server->subsystem)
		goto fail;

	status = shadow_subsystem_init(server->subsystem, server);
	if (status < 0)
		goto fail;

	return status;

fail:
	shadow_server_uninit(server);
	WLog_ERR(TAG, "Failed to initialize shadow server");
	return -1;
}

int shadow_server_uninit(rdpShadowServer* server)
{
	if (!server)
		return -1;

	shadow_server_stop(server);
	shadow_subsystem_uninit(server->subsystem);
	shadow_subsystem_free(server->subsystem);
	server->subsystem = NULL;
	freerdp_listener_free(server->listener);
	server->listener = NULL;
	free(server->CertificateFile);
	server->CertificateFile = NULL;
	free(server->PrivateKeyFile);
	server->PrivateKeyFile = NULL;
	free(server->ConfigPath);
	server->ConfigPath = NULL;
	DeleteCriticalSection(&(server->lock));
	(void)CloseHandle(server->StopEvent);
	server->StopEvent = NULL;
	ArrayList_Free(server->clients);
	server->clients = NULL;
	return 1;
}

rdpShadowServer* shadow_server_new(void)
{
	rdpShadowServer* server = NULL;
	server = (rdpShadowServer*)calloc(1, sizeof(rdpShadowServer));

	if (!server)
		return NULL;

	server->SupportMultiRectBitmapUpdates = TRUE;
	server->port = 3389;
	server->mayView = TRUE;
	server->mayInteract = TRUE;
	server->h264RateControlMode = H264_RATECONTROL_VBR;
	server->h264BitRate = 10000000;
	server->h264FrameRate = 30;
	server->h264QP = 0;
	server->authentication = TRUE;
	server->settings = freerdp_settings_new(FREERDP_SETTINGS_SERVER_MODE);
	return server;
}

void shadow_server_free(rdpShadowServer* server)
{
	if (!server)
		return;

	free(server->ipcSocket);
	server->ipcSocket = NULL;
	freerdp_settings_free(server->settings);
	server->settings = NULL;
	free(server);
}
