#include <Windows.h>
#include <tchar.h>
#include <stdio.h>
#include <Shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

bool get_config_file_name(TCHAR *config_file, unsigned int len)
{
	if (NULL == config_file)
	{
		_tprintf(L"config_file is null\n");
		return false;
	}

	TCHAR path[_MAX_PATH] = { 0 };
	if (!GetModuleFileName(NULL, path, sizeof(path)))
	{
		_tprintf(L"GetModuleFileName error: %d\n", GetLastError());
		return false;
	}

	TCHAR drive[_MAX_DRIVE] = { 0 };
	TCHAR dir[_MAX_DIR] = { 0 };
	TCHAR file[_MAX_FNAME] = { 0 };
	TCHAR ext[_MAX_EXT] = { 0 };
	errno_t err = 0;
	err = _tsplitpath_s(path, drive, _MAX_DRIVE, dir, _MAX_DIR, file, _MAX_FNAME, ext, _MAX_EXT);
	if (0 != err)
	{
		_tprintf(L"_tsplitpath_s error: %d\n", err);
		return false;
	}

	TCHAR config[_MAX_FNAME] = { 0 };
	_tcscpy_s(config, drive);
	_tcscat_s(config, dir);
	_tcscat_s(config, file);
	_tcscat_s(config, L".ini");

	_tcscpy_s(config_file, len, config);

	return true;
}

bool config_file_exists()
{
	TCHAR config[_MAX_PATH] = { 0 };
	get_config_file_name(config, _MAX_PATH);

	return (PathFileExists(config) ? true : false);
}

void gen_config_template()
{
	TCHAR config[_MAX_PATH] = { 0 };
	get_config_file_name(config, _MAX_PATH);

	FILE *fp = NULL;
	errno_t err = 0;
	err = _tfopen_s(&fp, config, L"w+");
	if (0 != err)
	{
		_tprintf(L"_tfopen_s error: %d\n", err);
		return;
	}

	_ftprintf_s(fp, L"%s\n", L";;for example");
	_ftprintf_s(fp, L"%s\n", L"; [log_path]");
	_ftprintf_s(fp, L"%s\n", L"; path=C:\\log.log");
	_ftprintf_s(fp, L"%s\n", L"; [keyword_list]");
	_ftprintf_s(fp, L"%s\n", L"; keyword=error");
	_ftprintf_s(fp, L"%s\n", L"");
	_ftprintf_s(fp, L"%s\n", L"[log_path]");
	_ftprintf_s(fp, L"%s\n", L"path=");
	_ftprintf_s(fp, L"%s\n", L"[keyword_list]");
	_ftprintf_s(fp, L"%s\n", L"keyword=");

	fclose(fp);
}

void query_info_from_config(TCHAR *path, unsigned int path_len, TCHAR *keyword, unsigned int keyword_len)
{
	if (NULL == path || NULL == keyword)
	{
		_tprintf(L"path or keyword is null\n");
		return;
	}

	TCHAR config[_MAX_PATH] = { 0 };
	get_config_file_name(config, _MAX_PATH);

	GetPrivateProfileString(L"log_path", L"path", NULL, path, path_len, config);
	GetPrivateProfileString(L"keyword_list", L"keyword", NULL, keyword, keyword_len, config);
}

void notify_keyword(TCHAR *path, unsigned int path_len, TCHAR *keyword, unsigned int keyword_len)
{
	if (NULL == path || NULL == keyword)
	{
		_tprintf(L"path or keyword is null\n");
		return;
	}

	TCHAR drive[_MAX_DRIVE] = { 0 };
	TCHAR dir[_MAX_DIR] = { 0 };
	errno_t err = 0;
	err = _tsplitpath_s(path, drive, _MAX_DRIVE, dir, _MAX_DIR, NULL, 0, NULL, 0);
	if (0 != err)
	{
		_tprintf(L"_tsplitpath_s error: %d\n", err);
		return;
	}

	TCHAR target_dir[_MAX_FNAME] = { 0 };
	_tcscpy_s(target_dir, drive);
	_tcscat_s(target_dir, dir);

	struct _stat last_stat = { 0 };
	_tstat(path, &last_stat);

	_off_t file_size = last_stat.st_size;

	HANDLE h = INVALID_HANDLE_VALUE;
	h = FindFirstChangeNotification(target_dir, FALSE, FILE_NOTIFY_CHANGE_SIZE);
	if (INVALID_HANDLE_VALUE == h)
	{
		_tprintf(L"FindFirstChangeNotification error: %d\n", GetLastError());
		return;
	}

	while (true)
	{
		WaitForSingleObject(h, INFINITE);

		// need to sleep before call _tstat, or open the file will fail
		Sleep(50);

		struct _stat new_stat = { 0 };
		_tstat(path, &new_stat);

		if (file_size != new_stat.st_size)
		{
			_tprintf(L"%s changed, new size: %d\n", path, new_stat.st_size);

			FILE *fp = NULL;
			errno_t err = 0;
			err = _tfopen_s(&fp, path, L"r");
			if (0 != err)
			{
				// why sometimes error 13 (Permission denied)?
				_tprintf(L"_tfopen_s error: %d\n", err);
				continue;
			}

			fseek(fp, file_size, SEEK_SET);

			TCHAR buff[1024] = { 0 };

			while (!feof(fp))
			{
				_fgetts(buff, 1024, fp);

				if (_tcsstr(buff, keyword))
					_tprintf(L"found \"%s\"\n", buff);
			}

			fclose(fp);
			file_size = new_stat.st_size;
		}

		if (FALSE == FindNextChangeNotification(h))
		{
			_tprintf(L"FindNextChangeNotification error: %d\n", GetLastError());
			break;
		}
	}

	FindCloseChangeNotification(h);
	return;
}

int main()
{
	if (!config_file_exists())
	{
		TCHAR config[_MAX_PATH] = { 0 };
		get_config_file_name(config, _MAX_PATH);

		_tprintf(L"config file not exists, now generate a new one in path:\n");
		_tprintf(L"%s\nplease edit it and run program again.\n", config);

		gen_config_template();
		return -1;
	}

	TCHAR path[_MAX_PATH] = { 0 };
	TCHAR keyword[_MAX_PATH] = { 0 };
	query_info_from_config(path, _MAX_PATH, keyword, _MAX_PATH);
	if (!(_tcslen(path) || _tcslen(keyword)))
	{
		_tprintf(L"path or keyword in config file is empty\n");
		return -1;
	}

	notify_keyword(path, _MAX_PATH, keyword, _MAX_PATH);

	return 0;
}
