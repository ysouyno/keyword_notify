#include <Windows.h>
#include <tchar.h>
#include <stdio.h>
#include <string>

#ifdef UNICODE
typedef std::wstring tstring;
#else
typedef std::string tstring;
#endif

bool get_config_file_name(tstring &config_file)
{
	config_file.erase();

	TCHAR path[_MAX_PATH] = { 0 };
	if (!GetModuleFileName(NULL, path, _MAX_PATH))
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

	config_file += drive;
	config_file += dir;
	config_file += file;
	config_file += L".ini";

	return true;
}

bool config_file_exists()
{
	tstring config;
	get_config_file_name(config);

	return (0 == _taccess_s(config.c_str(), 0) ? true : false);
}

void gen_config_template()
{
	tstring config;
	get_config_file_name(config);

	FILE *fp = NULL;
	errno_t err = 0;
	err = _tfopen_s(&fp, config.c_str(), L"w+");
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

void query_info_from_config(tstring &path, tstring &keyword)
{
	tstring config;
	get_config_file_name(config);

	TCHAR temp_path[_MAX_PATH] = { 0 };
	TCHAR temp_keyword[_MAX_PATH] = { 0 };
	GetPrivateProfileString(L"log_path", L"path", NULL, temp_path, _MAX_PATH, config.c_str());
	GetPrivateProfileString(L"keyword_list", L"keyword", NULL, temp_keyword, _MAX_PATH, config.c_str());

	path.erase();
	keyword.erase();

	path += temp_path;
	keyword += temp_keyword;
}

void notify_keyword(const tstring &path, const tstring keyword)
{
	tstring target_dir;
	target_dir = path.substr(0, path.find_last_of(L'\\'));

	struct _stat last_stat = { 0 };
	_tstat(path.c_str(), &last_stat);

	_off_t file_size = last_stat.st_size;

	HANDLE h = INVALID_HANDLE_VALUE;
	h = FindFirstChangeNotification(target_dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_SIZE);
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
		_tstat(path.c_str(), &new_stat);

		if (file_size != new_stat.st_size)
		{
			_tprintf(L"%s changed, new size: %d\n", path.c_str(), new_stat.st_size);

			FILE *fp = NULL;
			errno_t err = 0;
			err = _tfopen_s(&fp, path.c_str(), L"r");
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

				if (_tcsstr(buff, keyword.c_str()))
					_tprintf(L"%s\n", buff);
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
		tstring config;
		get_config_file_name(config);

		_tprintf(L"config file not exists, now generate a new one in path:\n");
		_tprintf(L"%s\nplease edit it and run program again.\n", config.c_str());

		gen_config_template();
		return -1;
	}

	tstring path;
	tstring keyword;
	query_info_from_config(path, keyword);
	if (path.empty() || keyword.empty())
	{
		_tprintf(L"path or keyword in config file is empty\n");
		return -1;
	}

	notify_keyword(path, keyword);

	return 0;
}
