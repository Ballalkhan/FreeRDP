#include <stdio.h>

#include <winpr/stream.h>
#include <winpr/path.h>
#include <winpr/crypto.h>

#include <freerdp/freerdp.h>
#include <freerdp/streamdump.h>

#include "../streamdump.h"

static BOOL test_entry_read_write(void)
{
	BOOL rc = FALSE;
	FILE* fp = NULL;
	wStream* sw = NULL;
	wStream* sr = NULL;
	size_t offset = 0;
	UINT64 ts = 0;
	UINT32 flags = 0;
	BYTE tmp[16] = { 0 };
	char tmp2[64] = { 0 };
	char* name = NULL;
	size_t entrysize = sizeof(UINT64) /* timestamp */ + sizeof(BYTE) /* direction */ +
	                   sizeof(UINT32) /* CRC */ + sizeof(UINT64) /* size */;

	winpr_RAND(tmp, sizeof(tmp));

	for (size_t x = 0; x < sizeof(tmp); x++)
		(void)_snprintf(&tmp2[x * 2], sizeof(tmp2) - 2 * x, "%02" PRIx8, tmp[x]);
	name = GetKnownSubPath(KNOWN_PATH_TEMP, tmp2);
	if (!name)
	{
		(void)fprintf(stderr, "[%s] Could not create temporary path\n", __func__);
		goto fail;
	}

	sw = Stream_New(NULL, 8123);
	sr = Stream_New(NULL, 1024);
	if (!sr || !sw)
	{
		(void)fprintf(stderr, "[%s] Could not create iostreams sw=%p, sr=%p\n", __func__, (void*)sw,
		              (void*)sr);
		goto fail;
	}

	winpr_RAND(Stream_Buffer(sw), Stream_Capacity(sw));
	entrysize += Stream_Capacity(sw);
	Stream_SetLength(sw, Stream_Capacity(sw));

	fp = fopen(name, "wb");
	if (!fp)
		goto fail;
	if (!stream_dump_write_line(fp, 0, sw))
		goto fail;
	(void)fclose(fp);

	fp = fopen(name, "rb");
	if (!fp)
		goto fail;
	if (!stream_dump_read_line(fp, sr, &ts, &offset, &flags))
		goto fail;

	if (entrysize != offset)
	{
		(void)fprintf(stderr, "[%s] offset %" PRIuz " bytes, entrysize %" PRIuz " bytes\n",
		              __func__, offset, entrysize);
		goto fail;
	}

	if (Stream_Length(sr) != Stream_Capacity(sw))
	{
		(void)fprintf(stderr, "[%s] Written %" PRIuz " bytes, read %" PRIuz " bytes\n", __func__,
		              Stream_Length(sr), Stream_Capacity(sw));
		goto fail;
	}

	if (memcmp(Stream_Buffer(sw), Stream_Buffer(sr), Stream_Capacity(sw)) != 0)
	{
		(void)fprintf(stderr, "[%s] Written data does not match data read back\n", __func__);
		goto fail;
	}
	rc = TRUE;
fail:
	Stream_Free(sr, TRUE);
	Stream_Free(sw, TRUE);
	if (fp)
		(void)fclose(fp);
	if (name)
		winpr_DeleteFile(name);
	free(name);
	(void)fprintf(stderr, "xxxxxxxxxxxxx %d\n", rc);
	return rc;
}

int TestStreamDump(int argc, char* argv[])
{
	WINPR_UNUSED(argc);
	WINPR_UNUSED(argv);

	if (!test_entry_read_write())
		return -1;
	return 0;
}
