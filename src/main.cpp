// chrlauncher
// Copyright (c) 2016 Henry++

#include <windows.h>

#include "main.h"
#include "rapp.h"
#include "routine.h"
#include "unzip.h"

#include "resource.h"

rapp app (APP_NAME, APP_NAME_SHORT, APP_VERSION, APP_COPYRIGHT);

#define CHROMIUM_UPDATE_URL L"http://chromium.woolyss.com/api/v3/?os=windows&bit=%d&type=%s&out=string"

STATIC_DATA config;

VOID _app_setpercent (HWND hwnd, DWORD v, DWORD t)
{
	UINT percent = 0;

	if (t)
		percent = static_cast<UINT>((double (v) / double (t)) * 100.0);

	SendDlgItemMessage (hwnd, IDC_PROGRESS, PBM_SETPOS, percent, 0);

	_r_status_settext (hwnd, IDC_STATUSBAR, 1, _r_fmt (L"%s/%s", _r_fmt_size64 (v), _r_fmt_size64 (t)));
}

VOID _app_setstatus (HWND hwnd, LPCWSTR text)
{
	_r_status_settext (hwnd, IDC_STATUSBAR, 0, text);
}

BOOL _app_installzip (HWND hwnd, LPCWSTR path)
{
	BOOL result = FALSE;
	ZIPENTRY ze = {0};
	const size_t title_length = wcslen (L"chrome-win32");

	HZIP hz = OpenZip (path, nullptr);

	if (!IsZipHandleU (hz))
	{
		WDBG1 (L"OpenZip failed.");
	}
	else
	{
		// count total files
		GetZipItem (hz, -1, &ze);
		INT total_files = ze.index;

		// check archive is right package
		GetZipItem (hz, 0, &ze);

		if (wcsncmp (ze.name, L"chrome-win32", title_length) == 0)
		{
			DWORD total_size = 0;
			DWORD total_read = 0; // this is our progress so far

			// count size of unpacked files
			for (INT i = 1; i < total_files; i++)
			{
				GetZipItem (hz, i, &ze);

				total_size += ze.unc_size;
			}

			rstring fpath;
			rstring fname;

			CHAR buffer[_R_BUFFER_LENGTH] = {0};

			DWORD written = 0;

			for (INT i = 1; i < total_files; i++)
			{
				GetZipItem (hz, i, &ze);

				fname = ze.name + title_length + 1;
				fpath.Format (L"%s\\%s", config.binary_dir, fname);

				_app_setpercent (hwnd, total_read, total_size);

				if ((ze.attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
				{
					_r_fs_mkdir (fpath);
				}
				else
				{
					HANDLE h = CreateFile (fpath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

					if (h != INVALID_HANDLE_VALUE)
					{
						DWORD total_read_file = 0;

						for (ZRESULT zr = ZR_MORE; zr == ZR_MORE;)
						{
							DWORD bufsize = _R_BUFFER_LENGTH;

							zr = UnzipItem (hz, static_cast<int>(i), buffer, bufsize);

							if (zr == ZR_OK)
							{
								bufsize = ze.unc_size - total_read_file;
							}

							buffer[bufsize] = 0;

							WriteFile (h, buffer, bufsize, &written, nullptr);

							total_read_file += bufsize;
							total_read += bufsize;
						}

						CloseHandle (h);
					}
				}
			}

			result = TRUE;
		}

		CloseZip (hz);
	}

	return result;
}

VOID _app_cleanup (LPCWSTR version)
{
	WIN32_FIND_DATA wfd = {0};
	HANDLE h = FindFirstFile (_r_fmt (L"%s\\*.manifest", config.binary_dir), &wfd);

	if (h != INVALID_HANDLE_VALUE)
	{
		size_t len = wcslen (version);

		do
		{
			if (_wcsnicmp (version, wfd.cFileName, len) != 0)
				DeleteFile (_r_fmt (L"%s\\%s", config.binary_dir, wfd.cFileName));
		}
		while (FindNextFile (h, &wfd));

		FindClose (h);
	}

	_r_fs_rmdir (_r_fmt (L"%s\\VisualElements", config.binary_dir));
}

BOOL _app_installupdate (HWND hwnd, LPCWSTR path)
{
	BOOL result = FALSE;
	BOOL is_ready = TRUE;

	// installing update
	_app_setstatus (hwnd, I18N (&app, IDS_STATUS_INSTALL, 0));
	_app_setpercent (hwnd, 0, 0);

	// check install folder for running processes
	while (TRUE)
	{
		is_ready = !_r_process_is_exists (config.binary_dir, wcslen (config.binary_dir));

		if (config.is_silent || is_ready || _r_msg (hwnd, MB_RETRYCANCEL | MB_ICONEXCLAMATION, APP_NAME, nullptr, I18N (&app, IDS_STATUS_CLOSEBROWSER, 0)) != IDRETRY)
			break;
	}

	// create directory
	if (is_ready)
	{
		if (!_r_fs_exists (config.binary_dir))
			_r_fs_mkdir (config.binary_dir);

		result = _app_installzip (hwnd, path);

		DeleteFile (path);
	}

	return result;
}

BOOL _app_downloadupdate (HWND hwnd, HINTERNET internet, LPCWSTR url, LPCWSTR path)
{
	BOOL result = FALSE;
	HINTERNET connect = nullptr;

	// download file
	_app_setstatus (hwnd, I18N (&app, IDS_STATUS_DOWNLOAD, 0));
	_app_setpercent (hwnd, 0, 0);

	if (internet && url)
	{
		connect = InternetOpenUrl (internet, url, nullptr, 0, INTERNET_FLAG_RESYNCHRONIZE | INTERNET_FLAG_NO_COOKIES, 0);

		if (!connect)
		{
			WDBG1 (L"InternetOpenUrl failed.");
		}
		else
		{
			DWORD status = 0, size = sizeof (status);
			HttpQueryInfo (connect, HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_STATUS_CODE, &status, &size, nullptr);

			if (status != HTTP_STATUS_OK)
			{
				WDBG1 (L"HttpQueryInfo failed.");
			}
			else
			{
				DWORD total_size = 0;

				size = sizeof (total_size);

				HttpQueryInfo (connect, HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_CONTENT_LENGTH, &total_size, &size, nullptr);

				HANDLE f = CreateFile (path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

				if (f != INVALID_HANDLE_VALUE)
				{
					CHAR buffera[_R_BUFFER_LENGTH] = {0};

					DWORD out = 0, written = 0, total_written = 0;

					while (TRUE)
					{
						if (!InternetReadFile (connect, buffera, _R_BUFFER_LENGTH - 1, &out) || !out)
							break;

						buffera[out] = 0;
						WriteFile (f, buffera, out, &written, nullptr);

						total_written += out;

						_app_setpercent (hwnd, total_written, total_size);
					}

					result = (total_size == total_written);

					CloseHandle (f);
				}
			}

			InternetCloseHandle (connect);
		}
	}

	return result;
}

UINT WINAPI _app_checkupdate (LPVOID lparam)
{
	HWND hwnd = (HWND)lparam;

	HINTERNET internet = nullptr;
	HINTERNET connect = nullptr;

	rstring::map_one result;

	UINT days = app.ConfigGet (L"ChromiumCheckPeriod", 1).AsUint ();
	BOOL is_exists = _r_fs_exists (config.binary_path);
	BOOL is_installsuccess = FALSE;

	if (config.is_forcecheck || (days || !is_exists) && !_r_process_is_exists (config.binary_dir, wcslen (config.binary_dir)))
	{
		if (config.is_forcecheck || !is_exists || (_r_unixtime_now () - app.ConfigGet (L"ChromiumCheckPeriodLast", 0).AsLonglong ()) >= (86400 * days))
		{
			internet = InternetOpen (app.GetUserAgent (), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);

			if (!internet)
			{
				WDBG1 (L"InternetOpen failed.");
			}
			else
			{
				connect = InternetOpenUrl (internet, _r_fmt (CHROMIUM_UPDATE_URL, config.architecture, config.type), nullptr, 0, INTERNET_FLAG_RESYNCHRONIZE | INTERNET_FLAG_NO_COOKIES, 0);

				if (!connect)
				{
					WDBG1 (L"InternetOpenUrl failed.");
				}
				else
				{
					DWORD status = 0, size = sizeof (status);
					HttpQueryInfo (connect, HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_STATUS_CODE, &status, &size, nullptr);

					if (status != HTTP_STATUS_OK)
					{
						WDBG1 (L"HttpQueryInfo failed.");
					}
					else
					{
						DWORD out = 0;

						CHAR buffera[_R_BUFFER_LENGTH] = {0};
						rstring bufferw;

						while (TRUE)
						{
							if (!InternetReadFile (connect, buffera, _R_BUFFER_LENGTH - 1, &out) || !out)
								break;

							buffera[out] = 0;
							bufferw.Append (buffera);
						}

						if (!bufferw.IsEmpty ())
						{
							rstring::rvector vc = bufferw.AsVector (L";");

							for (size_t i = 0; i < vc.size (); i++)
							{
								size_t pos = vc.at (i).Find (L'=');

								if (pos != rstring::npos)
									result[vc.at (i).Midded (0, pos)] = vc.at (i).Midded (pos + 1);
							}

							result[L"timestamp"] = _r_fmt_date (result[L"timestamp"].AsLonglong (), FDTF_SHORTDATE | FDTF_SHORTTIME);
						}
					}

					InternetCloseHandle (connect);
				}

				if (result.size ())
				{
					if (!is_exists || result[L"version"].CompareNoCase (config.version) != 0)
					{
						// get path
						WCHAR path[MAX_PATH] = {0};

						GetTempPath (MAX_PATH, path);
						GetTempFileName (path, nullptr, 0, path);

						// show info
						SetDlgItemText (hwnd, IDC_BROWSER, _r_fmt (I18N (&app, IDS_BROWSER, 0), config.name_full));
						SetDlgItemText (hwnd, IDC_CURRENTVERSION, _r_fmt (I18N (&app, IDS_CURRENTVERSION, 0), !config.version[0] ? L"<not found>" : config.version));
						SetDlgItemText (hwnd, IDC_VERSION, _r_fmt (I18N (&app, IDS_VERSION, 0), result[L"version"]));
						SetDlgItemText (hwnd, IDC_DATE, _r_fmt (I18N (&app, IDS_DATE, 0), result[L"timestamp"]));

						// show window
						_r_wnd_toggle (hwnd, TRUE);

						if (_app_downloadupdate (hwnd, internet, result[L"download"], path))
						{
							is_installsuccess = _app_installupdate (hwnd, path);
							_app_cleanup (result[L"version"]); // cleanup junk
						}
						else
						{
							if (config.is_silent)
								WDBG2 (I18N (&app, IDS_STATUS_NOTFOUND, 0), config.name_full);
							else
								_r_msg (hwnd, MB_OK | MB_ICONWARNING, APP_NAME, nullptr, I18N (&app, IDS_STATUS_NOTFOUND, 0), config.name_full);
						}
					}

					if (is_installsuccess || is_exists)
						app.ConfigSet (L"ChromiumCheckPeriodLast", _r_unixtime_now ());
				}
				else
				{
					if (config.is_silent)
						WDBG2 (I18N (&app, IDS_STATUS_NOTFOUND, 0), config.name_full);
					else
						_r_msg (hwnd, MB_OK | MB_ICONWARNING, APP_NAME, nullptr, I18N (&app, IDS_STATUS_NOTFOUND, 0), config.name_full);
				}

				InternetCloseHandle (internet);
			}
		}
	}

	PostMessage (hwnd, WM_DESTROY, 0, 0);

	return ERROR_SUCCESS;
}

rstring _app_getversion (LPCWSTR path)
{
	rstring result;

	DWORD verHandle;
	DWORD verSize = GetFileVersionInfoSize (path, &verHandle);

	if (verSize)
	{
		LPSTR verData = new char[verSize];

		if (GetFileVersionInfo (path, verHandle, verSize, verData))
		{
			LPBYTE buffer = nullptr;
			UINT size = 0;

			if (VerQueryValue (verData, L"\\", (VOID FAR* FAR*)&buffer, &size))
			{
				if (size)
				{
					VS_FIXEDFILEINFO *verInfo = (VS_FIXEDFILEINFO*)buffer;

					if (verInfo->dwSignature == 0xfeef04bd)
					{
						// Doesn't matter if you are on 32 bit or 64 bit,
						// DWORD is always 32 bits, so first two revision numbers
						// come from dwFileVersionMS, last two come from dwFileVersionLS

						result.Format (L"%d.%d.%d.%d", (verInfo->dwFileVersionMS >> 16) & 0xffff, (verInfo->dwFileVersionMS >> 0) & 0xffff, (verInfo->dwFileVersionLS >> 16) & 0xffff, (verInfo->dwFileVersionLS >> 0) & 0xffff);
					}
				}
			}
		}

		delete[] verData;
	}

	return result;
}

BOOL _app_run ()
{
	ExpandEnvironmentStrings (config.args, config.args, _countof (config.args));

	return _r_run (_r_fmt (L"\"%s\" %s", config.binary_path, config.args), config.binary_dir);
}

INT_PTR CALLBACK DlgProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
		case WM_INITDIALOG:
		{
			SetCurrentDirectory (app.GetDirectory ());

			// configure statusbar
			INT parts[] = {app.GetDPI (230), -1};
			SendDlgItemMessage (hwnd, IDC_STATUSBAR, SB_SETPARTS, 2, (LPARAM)parts);

			// parse command line
			INT argc = 0;
			LPWSTR* argv = CommandLineToArgvW (GetCommandLine (), &argc);

			if (argv)
			{
				for (int i = 0; i < argc; i++)
				{
					if (argv[i][0] == L'/' || argv[i][0] == L'-')
					{
						rstring name = rstring (argv[i]).Midded (1);

						if (name.CompareNoCase (L"q") == 0)
						{
							config.is_silent = TRUE;
						}
						else if (name.CompareNoCase (L"f") == 0)
						{
							config.is_forcecheck = TRUE;
						}
					}
				}

				LocalFree (argv);
			}

			// get browser architecture...
			config.architecture = app.ConfigGet (L"ChromiumArchitecture", 0).AsUint ();

			if (!config.architecture || (config.architecture != 64 && config.architecture != 32))
			{
				config.architecture = 0;

				// on XP only 32-bit supported
				if (_r_sys_validversion (5, 1, VER_EQUAL))
					config.architecture = 32;

				if (!config.architecture)
				{
					// ...by executable
					DWORD exe_type = 0;

					if (GetBinaryType (config.binary_path, &exe_type))
					{
						if (exe_type == SCS_32BIT_BINARY)
							config.architecture = 32;
						else if (exe_type == SCS_64BIT_BINARY)
							config.architecture = 64;
					}

					// ...by processor architecture
					SYSTEM_INFO si = {0};
					GetNativeSystemInfo (&si);

					if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
						config.architecture = 64;
					else
						config.architecture = 32;
				}
			}

			// configure paths
			StringCchCopy (config.type, _countof (config.type), app.ConfigGet (L"ChromiumType", L"dev-codecs-sync"));
			StringCchCopy (config.binary_dir, _countof (config.binary_dir), _r_normalize_path (app.ConfigGet (L"ChromiumDirectory", L".\\bin")));

			StringCchPrintf (config.name_full, _countof (config.name_full), L"Chromium %d-bit (%s)", config.architecture, config.type);
			StringCchPrintf (config.binary_path, _countof (config.binary_path), L"%s\\chrome.exe", config.binary_dir);

			StringCchCopy (config.args, _countof (config.args), app.ConfigGet (L"ChromiumCommandLine", L"--user-data-dir=..\\profile --no-default-browser-check --allow-outdated-plugins --disable-component-update"));

			StringCchCopy (config.version, _countof (config.version), _app_getversion (config.binary_path));

			rstring ppapi_path = _r_normalize_path (app.ConfigGet (L"FlashPlayerPath", L".\\plugins\\pepflashplayer.dll"));

			if (!ppapi_path.IsEmpty () && _r_fs_exists (ppapi_path))
			{
				rstring ppapi_version = _app_getversion (ppapi_path);

				StringCchCat (config.args, _countof (config.args), L" --ppapi-flash-path=\"");
				StringCchCat (config.args, _countof (config.args), ppapi_path);
				StringCchCat (config.args, _countof (config.args), L"\" --ppapi-flash-version=\"");
				StringCchCat (config.args, _countof (config.args), ppapi_version);
				StringCchCat (config.args, _countof (config.args), L"\"");
			}

			// parse command line
			INT numargs = 0;
			LPWSTR* arga = CommandLineToArgvW (GetCommandLine (), &numargs);

			rstring url;

			for (INT i = 1; i < numargs; i++)
			{
				if (_wcsicmp (arga[i], L"/url") == 0 || _wcsicmp (arga[i], L"/q") == 0 || _wcsicmp (arga[i], L"/f") == 0)
				{
					continue;
				}
				else if (arga[i][0] == L'-' && arga[i][1] == L'-')
				{
					StringCchCat (config.args, _countof (config.args), L" ");
					StringCchCat (config.args, _countof (config.args), arga[i]);
				}
				else
				{
					url = arga[i];
				}
			}

			if (!url.IsEmpty ())
			{
				StringCchCat (config.args, _countof (config.args), L" -- \"");
				StringCchCat (config.args, _countof (config.args), url);
				StringCchCat (config.args, _countof (config.args), L"\"");
			}

			LocalFree (arga);

			// start update checking
			_beginthreadex (nullptr, 0, &_app_checkupdate, hwnd, 0, nullptr);

			break;
		}

		case WM_CLOSE:
		{
			if (_r_msg (hwnd, MB_YESNO | MB_ICONQUESTION, APP_NAME, nullptr, I18N (&app, IDS_QUESTION_BUSY, 0)) != IDYES)
				return TRUE;

			DestroyWindow (hwnd);

			break;
		}

		case WM_DESTROY:
		{
			if (!_app_run ())
			{
				if (config.is_silent)
					WDBG2 (I18N (&app, IDS_STATUS_ERROR, 0), _r_dbg_error ());
				else
					_r_msg (hwnd, MB_OK | MB_ICONSTOP, APP_NAME, nullptr, I18N (&app, IDS_STATUS_ERROR, 0), _r_dbg_error ());
			}

			PostQuitMessage (0);

			break;
		}

		case WM_QUERYENDSESSION:
		{
			SetWindowLongPtr (hwnd, DWLP_MSGRESULT, TRUE);
			return TRUE;
		}

		case WM_LBUTTONDOWN:
		{
			SendMessage (hwnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
			break;
		}

		case WM_ENTERSIZEMOVE:
		case WM_EXITSIZEMOVE:
		case WM_CAPTURECHANGED:
		{
			LONG_PTR exstyle = GetWindowLongPtr (hwnd, GWL_EXSTYLE);

			if ((exstyle & WS_EX_LAYERED) == 0)
			{
				SetWindowLongPtr (hwnd, GWL_EXSTYLE, exstyle | WS_EX_LAYERED);
			}

			SetLayeredWindowAttributes (hwnd, 0, (msg == WM_ENTERSIZEMOVE) ? 100 : 255, LWA_ALPHA);
			SetCursor (LoadCursor (nullptr, (msg == WM_ENTERSIZEMOVE) ? IDC_SIZEALL : IDC_ARROW));

			break;
		}

		case WM_NOTIFY:
		{
			switch (LPNMHDR (lparam)->code)
			{
				case NM_CLICK:
				case NM_RETURN:
				{
					ShellExecute (nullptr, nullptr, PNMLINK (lparam)->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
					break;
				}
			}

			break;
		}

		case WM_COMMAND:
		{
			switch (LOWORD (wparam))
			{
				case IDCANCEL: // process Esc key
				case IDM_EXIT:
				{
					SendMessage (hwnd, WM_CLOSE, 0, 0);
					break;
				}

				case IDM_WEBSITE:
				{
					ShellExecute (hwnd, nullptr, _APP_WEBSITE_URL, nullptr, nullptr, SW_SHOWDEFAULT);
					break;
				}

				case IDM_DONATE:
				{
					ShellExecute (hwnd, nullptr, _APP_DONATION_URL, nullptr, nullptr, SW_SHOWDEFAULT);
					break;
				}

				case IDM_ABOUT:
				{
					app.CreateAboutWindow ();
					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

INT APIENTRY wWinMain (HINSTANCE, HINSTANCE, LPWSTR, INT)
{
	if (app.CreateMainWindow (&DlgProc, nullptr))
	{
		MSG msg = {0};

		while (GetMessage (&msg, nullptr, 0, 0))
		{
			if (!IsDialogMessage (app.GetHWND (), &msg))
			{
				TranslateMessage (&msg);
				DispatchMessage (&msg);
			}
		}
	}

	return ERROR_SUCCESS;
}
