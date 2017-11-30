#include <Windows.h>
#include <tchar.h>
#include <string>
#include <io.h> // for _taccess_s
#include <vector>

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
		_tprintf(_T("GetModuleFileName error: %d\n"), GetLastError());
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
		_tprintf(_T("_tsplitpath_s error: %d\n"), err);
		return false;
	}

	config_file += drive;
	config_file += dir;
	config_file += file;
	config_file += _T(".ini");

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
	err = _tfopen_s(&fp, config.c_str(), _T("w+"));
	if (0 != err)
	{
		_tprintf(_T("_tfopen_s error: %d\n"), err);
		return;
	}

	_ftprintf_s(fp, _T("%s\n"), _T(";;for example"));
	_ftprintf_s(fp, _T("%s\n"), _T("; [log_path]"));
	_ftprintf_s(fp, _T("%s\n"), _T("; path1=C:\\log.log"));
	_ftprintf_s(fp, _T("%s\n"), _T("; path2=D:\\log.log"));
	_ftprintf_s(fp, _T("%s\n"), _T("; [keyword_list]"));
	_ftprintf_s(fp, _T("%s\n"), _T("; keyword1=error1"));
	_ftprintf_s(fp, _T("%s\n"), _T("; keyword2=error2"));
	_ftprintf_s(fp, _T("%s\n"), _T(""));
	_ftprintf_s(fp, _T("%s\n"), _T("[log_path]"));
	_ftprintf_s(fp, _T("%s\n"), _T("path1="));
	_ftprintf_s(fp, _T("%s\n"), _T("[keyword_list]"));
	_ftprintf_s(fp, _T("%s\n"), _T("keyword1="));

	fclose(fp);
}

void query_info_from_config(tstring &path, tstring &keyword)
{
	tstring config;
	get_config_file_name(config);

	TCHAR temp_path[_MAX_PATH] = { 0 };
	TCHAR temp_keyword[_MAX_PATH] = { 0 };
	GetPrivateProfileString(_T("log_path"), _T("path"), NULL, temp_path, _MAX_PATH, config.c_str());
	GetPrivateProfileString(_T("keyword_list"), _T("keyword"), NULL, temp_keyword, _MAX_PATH, config.c_str());

	path.erase();
	keyword.erase();

	path += temp_path;
	keyword += temp_keyword;
}

void query_info_from_config(std::vector<tstring> &path_vec, std::vector<tstring> &keyword_vec)
{
	tstring config;
	get_config_file_name(config);

	TCHAR key_name[_MAX_FNAME] = { 0 };
	TCHAR temp_path[_MAX_PATH] = { 0 };
	for (int i = 0; i < 64; ++i)
	{
		_stprintf_s(key_name, _T("path%d"), i + 1);

		DWORD ret = GetPrivateProfileString(_T("log_path"), key_name, NULL, temp_path, _MAX_PATH, config.c_str());
		if (0 == ret)
			break;

		tstring str(temp_path);
		path_vec.push_back(str);
	}

	TCHAR temp_keyword[_MAX_PATH] = { 0 };
	for (int i = 0; i < 64; ++i)
	{
		_stprintf_s(key_name, _T("keyword%d"), i + 1);

		DWORD ret = GetPrivateProfileString(_T("keyword_list"), key_name, NULL, temp_keyword, _MAX_PATH, config.c_str());
		if (0 == ret)
			break;

		tstring str(temp_keyword);
		keyword_vec.push_back(str);
	}
}

void notify_keyword(const tstring &path, const tstring &keyword)
{
	tstring target_dir;
	target_dir = path.substr(0, path.find_last_of(_T('\\')));

	struct _stat last_stat = { 0 };
	_tstat(path.c_str(), &last_stat);

	_off_t file_size = last_stat.st_size;

	HANDLE h = INVALID_HANDLE_VALUE;
	h = FindFirstChangeNotification(target_dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_SIZE);
	if (INVALID_HANDLE_VALUE == h)
	{
		_tprintf(_T("FindFirstChangeNotification error: %d\n"), GetLastError());
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
			_tprintf(_T("%s changed, new size: %d\n"), path.c_str(), new_stat.st_size);

			FILE *fp = NULL;
			errno_t err = 0;
			err = _tfopen_s(&fp, path.c_str(), _T("r"));
			if (0 != err)
			{
				// why sometimes error 13 (Permission denied)?
				_tprintf(_T("_tfopen_s error: %d\n"), err);
				continue;
			}

			fseek(fp, file_size, SEEK_SET);

			TCHAR buff[1024] = { 0 };

			while (!feof(fp))
			{
				_fgetts(buff, 1024, fp);

				if (_tcsstr(buff, keyword.c_str()))
					_tprintf(_T("%s\n"), buff);
			}

			fclose(fp);
			file_size = new_stat.st_size;
		}

		if (FALSE == FindNextChangeNotification(h))
		{
			_tprintf(_T("FindNextChangeNotification error: %d\n"), GetLastError());
			break;
		}
	}

	FindCloseChangeNotification(h);
	return;
}

typedef struct _thread_param
{
	tstring path;
	tstring keyword;
} thread_param, *pthread_param;

DWORD WINAPI thread_proc(LPVOID param)
{
	pthread_param pp = (pthread_param)param;
	_tprintf(_T("%s, %s\n"), pp->path.c_str(), pp->keyword.c_str());

	tstring target_dir;
	target_dir = pp->path.substr(0, pp->path.find_last_of(_T('\\')));

	struct _stat last_stat = { 0 };
	_tstat(pp->path.c_str(), &last_stat);

	_off_t file_size = last_stat.st_size;

	HANDLE h = INVALID_HANDLE_VALUE;
	h = FindFirstChangeNotification(target_dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_SIZE);
	if (INVALID_HANDLE_VALUE == h)
	{
		_tprintf(_T("FindFirstChangeNotification error: %d\n"), GetLastError());
		return -1;
	}

	while (true)
	{
		WaitForSingleObject(h, INFINITE);

		// need to sleep before call _tstat, or open the file will fail
		Sleep(50);

		struct _stat new_stat = { 0 };
		_tstat(pp->path.c_str(), &new_stat);

		if (file_size != new_stat.st_size)
		{
			_tprintf(_T("%s changed, new size: %d\n"), pp->path.c_str(), new_stat.st_size);

			FILE *fp = NULL;
			errno_t err = 0;
			err = _tfopen_s(&fp, pp->path.c_str(), _T("r"));
			if (0 != err)
			{
				// why sometimes error 13 (Permission denied)?
				_tprintf(_T("_tfopen_s error: %d\n"), err);
				continue;
			}

			fseek(fp, file_size, SEEK_SET);

			TCHAR buff[1024] = { 0 };

			while (!feof(fp))
			{
				_fgetts(buff, 1024, fp);

				if (_tcsstr(buff, pp->keyword.c_str()))
					_tprintf(_T("%s\n"), buff);
			}

			fclose(fp);
			file_size = new_stat.st_size;
		}

		if (FALSE == FindNextChangeNotification(h))
		{
			_tprintf(_T("FindNextChangeNotification error: %d\n"), GetLastError());
			break;
		}
	}

	FindCloseChangeNotification(h);
	delete pp;
	return 0;
}

void notify_keyword(const std::vector<tstring> &path_vec, const std::vector<tstring> &keyword_vec)
{
	for (std::vector<tstring>::const_iterator path_citer = path_vec.cbegin(); path_citer != path_vec.cend(); ++path_citer)
	{
		for (std::vector<tstring>::const_iterator keyword_citer = keyword_vec.cbegin(); keyword_citer != keyword_vec.cend(); ++keyword_citer)
		{
			thread_param *ptp = new thread_param;
			ptp->path = *path_citer;
			ptp->keyword = *keyword_citer;

			HANDLE h = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)thread_proc, ptp, 0, NULL);
			if (NULL != h)
				CloseHandle(h);
		}
	}
}

int main()
{
	if (!config_file_exists())
	{
		tstring config;
		get_config_file_name(config);

		_tprintf(_T("config file not exists, now generate a new one in path:\n"));
		_tprintf(_T("%s\nplease edit it and run program again.\n"), config.c_str());

		gen_config_template();
		return -1;
	}

	/*tstring path;
	tstring keyword;
	query_info_from_config(path, keyword);
	if (path.empty() || keyword.empty())
	{
		_tprintf(_T("path or keyword in config file is empty\n"));
		return -1;
	}*/

	std::vector<tstring> path_vec;
	std::vector<tstring> keyword_vec;
	query_info_from_config(path_vec, keyword_vec);

	/*notify_keyword(path, keyword);*/
	notify_keyword(path_vec, keyword_vec);

	system("pause");

	return 0;
}
