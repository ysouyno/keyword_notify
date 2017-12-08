#include <Windows.h>
#include <tchar.h>
#include <string>
#include <io.h> // for _taccess_s
#include <map>
#include <list>
#include <signal.h>
#include <vector>

#ifdef UNICODE
typedef std::wstring tstring;
#else
typedef std::string tstring;
#endif

const size_t max_files_count = 8;
const size_t max_keywords_count = 8;
static volatile bool running = true;

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
	_ftprintf_s(fp, _T("%s\n"), _T(""));
	_ftprintf_s(fp, _T("%s\n"), _T("; [target_file1]"));
	_ftprintf_s(fp, _T("%s\n"), _T("; path=C:\\log.log"));
	_ftprintf_s(fp, _T("%s\n"), _T("; keyword1=error1"));
	_ftprintf_s(fp, _T("%s\n"), _T("; keyword2=error2"));
	_ftprintf_s(fp, _T("%s\n"), _T(""));
	_ftprintf_s(fp, _T("%s\n"), _T("; [target_file2]"));
	_ftprintf_s(fp, _T("%s\n"), _T("; path=D:\\log.log"));
	_ftprintf_s(fp, _T("%s\n"), _T("; keyword1=warn1"));

	fclose(fp);
}

void query_info_from_config(std::map<tstring, std::list<tstring> > &info_map)
{
	tstring config;
	get_config_file_name(config);

	TCHAR app_name[_MAX_FNAME] = { 0 };
	TCHAR target_file[_MAX_PATH] = { 0 };
	for (int i = 0; i < max_files_count; ++i)
	{
		_stprintf_s(app_name, _T("target_file%d"), i + 1);

		DWORD ret = GetPrivateProfileString(app_name, _T("path"), NULL, target_file, _MAX_PATH,
			config.c_str());
		if (0 == ret)
			break;

		std::list<tstring> keywords_list;

		TCHAR key_name[_MAX_FNAME] = { 0 };
		TCHAR temp_key[_MAX_FNAME] = { 0 };
		for (int j = 0; j < max_keywords_count; ++j)
		{
			_stprintf_s(key_name, _T("keyword%d"), j + 1);

			DWORD ret = GetPrivateProfileString(app_name, key_name, NULL, temp_key, _MAX_FNAME,
				config.c_str());
			if (0 == ret)
				break;

			tstring str(temp_key);
			keywords_list.push_back(str);
		}

		info_map.insert(std::pair<tstring, std::list<tstring> >(target_file, keywords_list));
	}
}

typedef struct _thread_param
{
	tstring path;
	std::list<tstring> keywords_list;
} thread_param, *pthread_param;

enum
{
	event_handle = 0,
	notification_handle
};

DWORD WINAPI thread_proc(LPVOID param)
{
	pthread_param pp = (pthread_param)param;

	tstring target_dir;
	target_dir = pp->path.substr(0, pp->path.find_last_of(_T('\\')));

	struct _stat last_stat = { 0 };
	_tstat(pp->path.c_str(), &last_stat);

	_off_t file_size = last_stat.st_size;

	HANDLE h[2] = { 0 };
	h[event_handle] = CreateEvent(NULL, TRUE, FALSE, _T("named_event"));

	h[notification_handle] = FindFirstChangeNotification(target_dir.c_str(), FALSE,
		FILE_NOTIFY_CHANGE_SIZE);
	if (INVALID_HANDLE_VALUE == h[notification_handle])
	{
		_tprintf(_T("FindFirstChangeNotification error: %d\n"), GetLastError());
		return -1;
	}

	DWORD wait_status = 0;

	while (true)
	{
		wait_status = WaitForMultipleObjects(2, h, FALSE, INFINITE);

		// need to sleep before call _tstat, or open the file will fail
		Sleep(50);

		switch (wait_status)
		{
		case WAIT_OBJECT_0 + event_handle:
			_tprintf(_T("get stop event\n"));
			delete pp;
			CloseHandle(h[event_handle]);
			FindCloseChangeNotification(h[notification_handle]);
			return 0;
		case WAIT_OBJECT_0 + notification_handle:
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

					for (std::list<tstring>::iterator iter = pp->keywords_list.begin();
						iter != pp->keywords_list.end(); ++iter)
					{
						if (_tcsstr(buff, iter->c_str()))
							_tprintf(_T("%s\n"), buff);
					}
				}

				fclose(fp);
				file_size = new_stat.st_size;
			}

			if (FALSE == FindNextChangeNotification(h[notification_handle]))
			{
				_tprintf(_T("FindNextChangeNotification error: %d\n"), GetLastError());
				break;
			}
			break;
		}
	}

	delete pp;
	CloseHandle(h[event_handle]);
	FindCloseChangeNotification(h[notification_handle]);

	return 0;
}

void notify_keyword(const std::map<tstring, std::list<tstring> > &info_map, std::vector<HANDLE> &handle_vec)
{
	for (std::map<tstring, std::list<tstring> >::const_iterator citer = info_map.cbegin();
		citer != info_map.cend(); ++citer)
	{
		thread_param *ptp = new thread_param;
		ptp->path = citer->first;
		ptp->keywords_list = citer->second;

		HANDLE h = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)thread_proc, ptp, 0, NULL);
		if (NULL != h)
			handle_vec.push_back(h);
	}
}

void signal_handler(int signal)
{
	HANDLE h_event = CreateEvent(NULL, TRUE, FALSE, _T("named_event"));
	SetEvent(h_event);
	CloseHandle(h_event);

	running = false;
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

	std::map<tstring, std::list<tstring> > info_map;
	query_info_from_config(info_map);

	std::vector<HANDLE> handle_vec;
	notify_keyword(info_map, handle_vec);

	if (handle_vec.empty())
	{
		_tprintf(_T("no keywords to notify, now quit\n"));
		return -1;
	}

	HANDLE h_event = CreateEvent(NULL, TRUE, FALSE, _T("named_event"));

	signal(SIGINT, signal_handler);
	while (running)
		;

	_tprintf(_T("theads stopping...\n"));

	HANDLE h[max_files_count] = { 0 };
	for (size_t i = 0; i < handle_vec.size(); ++i)
		h[i] = handle_vec.at(i);

	WaitForMultipleObjects((DWORD)handle_vec.size(), h, TRUE, 1000);

	_tprintf(_T("all threads stopped\n"));

	for (size_t i = 0; i < handle_vec.size(); ++i)
		CloseHandle(h[i]);

	CloseHandle(h_event);

	return 0;
}
