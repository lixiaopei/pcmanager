#include "stdafx.h"
#include "SoftUninstall.h"
#include "SoftUninstallApi.h"
#include "SoftUninstallSql.h"
using namespace Skylark;
#include <winstl/registry/reg_key.hpp>
#include <winstl/registry/reg_value.hpp>
#include <winstl/registry/reg_key_sequence.hpp>
#include <winstl/filesystem/filesystem_traits.hpp>
using namespace winstl;
#include <stlsoft/algorithms/unordered.hpp>
#include <stlsoft/memory/auto_buffer.hpp>
#include <stlsoft/string/case_functions.hpp>
#include <stlsoft/string/trim_functions.hpp>
#include <stlsoft/system/commandline_parser.hpp>
using namespace stlsoft;
#include <algorithm>
using namespace std;
#include <shlobj.h>
#include <ShellAPI.h>
#pragma comment(lib, "shell32.lib")
#include <Shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include <comstl/util/creation_functions.hpp>
using namespace comstl;

namespace ksm
{

typedef reg_key_sequence_w::const_iterator reg_const_iter;

//
// SoftData2默认排序比较谓词
//
class SoftData2DefSort
{
public:
	bool operator()(const SoftData2 &left, const SoftData2 &right) const 
	{ return (_wcsicmp(left._displayName.c_str(), right._displayName.c_str()) < 0); }
};

//
// SoftData2比较谓词
//
class SoftData2KeySort
{
public:
	bool operator()(const SoftData2 &left, const SoftData2 &right) const 
	{ return left._key < right._key; }
};

class SoftData2NameSort
{
public:
	bool operator()(const SoftData2 &left, const SoftData2 &right) const 
	{ return left._displayName < right._displayName; }
};

//
// SoftData2查找谓词
//
class SoftData2GuidFind
{
public:
	SoftData2GuidFind(const wstring &guid) : _guid(guid) {}

	bool operator()(const SoftData2 &softData2) const 
	{ return _guid == softData2._guid; }

private:
	const wstring &_guid;
};

class SoftData2KeyCmp : public binary_function<SoftData2, SoftData2, bool>
{
public:
	bool operator()(const SoftData2 &left, const SoftData2 &right) const 
	{ return left._key == right._key; }
};

class SoftData2NameUninstFind
{
public:
	bool operator()(const SoftData2 &left, const SoftData2 &right) const 
	{ return ((left._displayName == right._displayName) && (left._uninstString == right._uninstString)); }
};

// 内部函数
static BOOL __RegRecurseDeleteKey(HKEY hKeyRoot, LPTSTR lpSubKey, REGSAM samDesired);

// 软件类型判断
static BOOL CheckSoftType(ISQLiteComResultSet3 *pRs, SoftData2 &softData2, int &matchType, LPCWSTR &pPattern);

Cx64Api::PFN_RegDeleteKeyExW Cx64Api::sRegDeleteKeyExW = NULL;
CFsRedDisScoped::PFN_Wow64RevertWow64FsRedirection CFsRedDisScoped::spfnWow64RevertWow64FsRedirection = NULL;
CFsRedDisScoped::PFN_Wow64DisableWow64FsRedirection CFsRedDisScoped::spfnWow64DisableWow64FsRedirection = NULL;
//////////////////////////////////////////////////////////////////////////
BOOL LoadSoftUninstData(Skylark::ISQLiteComDatabase3 *pDB, SoftData2List &sfotData2List)
{
	CComPtr<ISQLiteComResultSet3> pRs;
	HRESULT hr = pDB->ExecuteQuery(L"select * from local_soft_list", &pRs);
	if(!SUCCEEDED(hr))
		return FALSE;

	while(!pRs->IsEof())
	{
		sfotData2List.push_back(SoftData2());
		SoftData2 &softData2 = sfotData2List.back();

		softData2._mask				= SDM_All;
		softData2._key				= pRs->GetAsString(L"soft_key");
		softData2._guid				= pRs->GetAsString(L"guid");
		softData2._displayName		= pRs->GetAsString(L"display_name");
		softData2._mainPath			= pRs->GetAsString(L"main_path");
		softData2._descript			= pRs->GetAsString(L"descript");
		softData2._descriptReg		= pRs->GetAsString(L"descript_reg");
		softData2._infoUrl			= pRs->GetAsString(L"info_url");
		softData2._spellWhole		= pRs->GetAsString(L"spell_whole");
		softData2._spellAcronym		= pRs->GetAsString(L"spell_acronym");
		softData2._iconLocation		= pRs->GetAsString(L"icon_location");
		softData2._uninstString		= pRs->GetAsString(L"uninstall_string");
		softData2._logoUrl			= pRs->GetAsString(L"logo_url");
		softData2._size				= pRs->GetInt64(L"size");
		softData2._lastUse			= static_cast<LONG>(pRs->GetInt(L"last_use"));
		softData2._type				= static_cast<LONG>(pRs->GetInt(L"type_id"));
		softData2._id				= static_cast<LONG>(pRs->GetInt(L"soft_id"));
		softData2._count			= static_cast<LONG>(pRs->GetInt(L"daycnt"));

		pRs->NextRow();
	}

//	sfotData2List.sort(SoftData2DefSort());
	return TRUE;
}

BOOL LoadSoftUninstDataByKey( Skylark::ISQLiteComDatabase3 *pDB, LPCWSTR lpKey, SoftData2 &softData2 )
{

	CComPtr<ISQLiteComStatement3> pState;
	HRESULT hr = pDB->PrepareStatement(L"select * from local_soft_list where soft_key=?;", &pState);
	if(!SUCCEEDED(hr)) return FALSE;

	pState->Bind(1, lpKey);

	CComPtr<ISQLiteComResultSet3> pRs;
	hr = pState->ExecuteQuery(&pRs);
	if(!SUCCEEDED(hr)) return FALSE;

	if(!pRs->IsEof())
	{
		softData2._mask				= SDM_All;
		softData2._key				= pRs->GetAsString(L"soft_key");
		softData2._guid				= pRs->GetAsString(L"guid");
		softData2._displayName		= pRs->GetAsString(L"display_name");
		softData2._mainPath			= pRs->GetAsString(L"main_path");
		softData2._descript			= pRs->GetAsString(L"descript");
		softData2._infoUrl			= pRs->GetAsString(L"info_url");
		softData2._spellWhole		= pRs->GetAsString(L"spell_whole");
		softData2._spellAcronym		= pRs->GetAsString(L"spell_acronym");
		softData2._iconLocation		= pRs->GetAsString(L"icon_location");
		softData2._uninstString		= pRs->GetAsString(L"uninstall_string");
		softData2._logoUrl			= pRs->GetAsString(L"logo_url");
		softData2._size				= pRs->GetInt64(L"size");
		softData2._lastUse			= static_cast<LONG>(pRs->GetInt(L"last_use"));
		softData2._type				= static_cast<LONG>(pRs->GetInt(L"type_id"));
		softData2._id				= static_cast<LONG>(pRs->GetInt(L"soft_id"));
		softData2._count			= static_cast<LONG>(pRs->GetInt(L"daycnt"));

		return TRUE;
	}

	return FALSE;
}

BOOL LoadLinkData(Skylark::ISQLiteComDatabase3 *pDB, SoftItemAttri type, WStrList &linkList)
{
	CComPtr<ISQLiteComStatement3> pState;
	HRESULT hr = pDB->PrepareStatement(L"select soft_key from link_list where type=?", &pState);
	if(!SUCCEEDED(hr)) return FALSE;

	pState->Bind(1, static_cast<int>(type));

	CComPtr<ISQLiteComResultSet3> pRs;
	hr = pState->ExecuteQuery(&pRs);
	if(!SUCCEEDED(hr)) return FALSE;

	while(!pRs->IsEof())
	{
		linkList.push_back(wstring());
		linkList.back() = pRs->GetAsString(L"soft_key");

		pRs->NextRow();
	}

	return TRUE;
}

BOOL Hex2Guid(const wstring &strHex, wstring &strGuid)
{
	if(strHex.size() < 32) return FALSE;
	for(int i = 0; i < 32; ++i)
	{
		if(iswxdigit(strHex[i]) == 0) 
			return FALSE;
	}

	strGuid.reserve(32 + 6);
	strGuid += L'{';
	strGuid = strGuid + strHex[7] + strHex[6] + strHex[5] + strHex[4] + strHex[3] + strHex[2] + strHex[1] + strHex[0];
	strGuid += L'-';
	strGuid = strGuid + strHex[11] + strHex[10] + strHex[9] + strHex[8];
	strGuid += L'-';
	strGuid = strGuid + strHex[15] + strHex[14] + strHex[13] + strHex[12];
	strGuid += L'-';
	strGuid = strGuid + strHex[17] + strHex[16] + strHex[19] + strHex[18];
	strGuid += L'-';
	strGuid = strGuid + strHex[21] + strHex[20] + strHex[23] + strHex[22] + strHex[25] + strHex[24] + strHex[27] + strHex[26] + strHex[29] + strHex[28] + strHex[31] + strHex[30];
	strGuid += L'}';	

	make_lower(strGuid);
	return TRUE;
}

BOOL Guid2Hex(const std::wstring &strGuid, std::wstring &strHex)
{
	if(!IsGuid(strGuid)) return FALSE;

	strHex.reserve(32);
	strHex += strGuid[8];
	strHex += strGuid[7];
	strHex += strGuid[6];
	strHex += strGuid[5];
	strHex += strGuid[4];
	strHex += strGuid[3];
	strHex += strGuid[2];
	strHex += strGuid[1];
	strHex += strGuid[13];
	strHex += strGuid[12];
	strHex += strGuid[11];
	strHex += strGuid[10];
	strHex += strGuid[18];
	strHex += strGuid[17];
	strHex += strGuid[16];
	strHex += strGuid[15];
	strHex += strGuid[21];
	strHex += strGuid[20];
	strHex += strGuid[23];
	strHex += strGuid[22];
	strHex += strGuid[26];
	strHex += strGuid[25];
	strHex += strGuid[28];
	strHex += strGuid[27];
	strHex += strGuid[30];
	strHex += strGuid[29];
	strHex += strGuid[32];
	strHex += strGuid[31];
	strHex += strGuid[34];
	strHex += strGuid[33];
	strHex += strGuid[36];
	strHex += strGuid[35];

	return TRUE;
}

CSensitivePaths::CSensitivePaths()
{
	const static int folders[] = 
	{
		CSIDL_WINDOWS,
		CSIDL_SYSTEM,
		CSIDL_DRIVES,
		CSIDL_PROGRAMS,
		CSIDL_PROGRAM_FILES,
		CSIDL_COMMON_PROGRAMS,
		CSIDL_STARTUP,
		CSIDL_COMMON_STARTUP,
		CSIDL_DESKTOP,
		CSIDL_DESKTOPDIRECTORY,
		CSIDL_COMMON_DESKTOPDIRECTORY,
		CSIDL_APPDATA,
	};

	auto_buffer<wchar_t> buffer(MAX_PATH);
	for(int i = 0; i < STLSOFT_NUM_ELEMENTS(folders); ++i)
	{
		if(::SHGetSpecialFolderPathW(NULL, &buffer[0], folders[i], FALSE))
		{
			wstring path = MakeAbsolutePath(&buffer[0]);
			trim_right(path, L"\\");
			make_lower(path);

			_pathHash.insert(path);
		}
	}

	// 其它特殊目录
	if(::SHGetSpecialFolderPathW(NULL, &buffer[0], CSIDL_APPDATA, FALSE))
	{
		wstring path = MakeAbsolutePath(&buffer[0]);
		trim_right(path, L"\\");
		make_lower(path);

		_pathHash.insert(path + L"\\microsoft\\internet explorer\\quick launch");
	}

	if(::SHGetSpecialFolderPathW(NULL, &buffer[0], CSIDL_WINDOWS, FALSE))
	{
		wstring path = MakeAbsolutePath(&buffer[0]);
		trim_right(path, L"\\");
		make_lower(path);

		_pathHash.insert(path + L"\\installshield installation information");
	}
}

BOOL _RegDeleteValue(HKEY hKeyRoot, LPCTSTR lpSubKey, LPCTSTR lpValueName, REGSAM samDesired)
{
	HKEY hSubKey;
	LONG ret = ::RegOpenKeyEx(hKeyRoot, lpSubKey, 0, samDesired, &hSubKey);
	if(ret == ERROR_SUCCESS)
	{
		::RegDeleteValueW(hSubKey, lpValueName);
		::RegCloseKey(hSubKey);
		return TRUE;
	}
	else if(ret == ERROR_FILE_NOT_FOUND)
	{
		return TRUE;
	}

	return FALSE;
}

BOOL RegRecurseDeleteKey(HKEY hKeyRoot, LPCTSTR lpSubKey, REGSAM samDesired)
{
	TCHAR szDelKey[2 * MAX_PATH]; 
	_tcscpy_s(szDelKey, MAX_PATH * 2, lpSubKey);

	return __RegRecurseDeleteKey(hKeyRoot, szDelKey, samDesired | KEY_ENUMERATE_SUB_KEYS);
}

BOOL __RegRecurseDeleteKey(HKEY hKeyRoot, LPTSTR lpSubKey, REGSAM samDesired)
{
	// First, see if we can delete the key without having
	// to recurse.
	LONG lResult = Cx64Api::RegDeleteKeyExW(hKeyRoot, lpSubKey, samDesired, 0);
	if (lResult == ERROR_SUCCESS) return TRUE;

	HKEY hKey;
	lResult = ::RegOpenKeyEx (hKeyRoot, lpSubKey, 0, samDesired, &hKey);
	if (lResult != ERROR_SUCCESS) 
	{
		if (lResult == ERROR_FILE_NOT_FOUND) return TRUE;
		else return FALSE;
	}

	// Check for an ending slash and add one if it is missing.
	LPTSTR lpEnd = lpSubKey + lstrlen(lpSubKey);
	if (*(lpEnd - 1) != _T('\\')) 
	{
		*lpEnd =  _T('\\');
		lpEnd++;
		*lpEnd =  _T('\0');
	}

	// Enumerate the keys
	FILETIME ftWrite;
	TCHAR szName[MAX_PATH];
	DWORD dwSize = MAX_PATH;

	lResult = ::RegEnumKeyEx(hKey, 0, szName, &dwSize, NULL, NULL, NULL, &ftWrite);
	if (lResult == ERROR_SUCCESS) 
	{
		do 
		{
			_tcscat_s(lpSubKey, MAX_PATH*2, szName);
			if (!__RegRecurseDeleteKey(hKeyRoot, lpSubKey, samDesired)) break;

			dwSize = MAX_PATH;
			lResult = ::RegEnumKeyEx(hKey, 0, szName, &dwSize, NULL, NULL, NULL, &ftWrite);

			// Recovery the subkey's name
			*lpEnd = _T('\0');
		} 
		while (lResult == ERROR_SUCCESS);
	}

	// Recovery the subkey's name without the ending slash
	lpEnd--; *lpEnd = _T('\0');

	::RegCloseKey (hKey);

	// Try again to delete the key.
	lResult = Cx64Api::RegDeleteKeyExW(hKeyRoot, lpSubKey, samDesired, 0);
	if (lResult == ERROR_SUCCESS) return TRUE;

	return FALSE;
}

BOOL RegListKey(HKEY hKey, const std::wstring &sub, std::list<std::wstring> &keyList, REGSAM samDesired)
{
	try
	{
		reg_key_sequence_w key(hKey, sub.c_str(), samDesired);

		reg_const_iter end = key.end();
		for(reg_const_iter it = key.begin(); it != end; ++it)
		{
			keyList.push_back(it.get_key_name());
		}

		return TRUE;
	}
	catch(...) {}
	return FALSE;
}

CSearchSoftData::CSearchSoftData(SoftData2List &softData2List) 
	: _softData2List(softData2List) 
{
	try
	{
		//
		// 搜索32位软件（32位与64位可能混合存在）
		//
		{
			// 1.搜索Installer
			SoftData2List installer;
			SearchInstaller(installer);

			// 2.搜索Uninstall
			SoftData2List uninstall;
			SearchUninstall(uninstall);

			// 3.合并结果
			// 以Uninstall为主
			SoftData2Iter end = uninstall.end();
			for(SoftData2Iter it = uninstall.begin(); it != end; ++it)
			{
				// 修正Key
				it->_key = L"uninstall\\" + it->_key;

				// Uninstall与Installer以Guid联系
				if(it->_guid.empty()) continue;

				SoftData2Iter it2 = find_if(installer.begin(), installer.end(), SoftData2GuidFind(it->_guid));
				if(it2 != installer.end())
				{
					MergeSoftData2(*it, *it2);
					installer.erase(it2);
				}
			}

			softData2List.splice(softData2List.end(), uninstall);

			// 修正并排除Installer
			end = installer.end();
			for(SoftData2Iter it = installer.begin(); it != end;)
			{
				if(_excluded.find(it->_guid) != _excluded.end())
				{
					installer.erase(it++);
					continue;
				}

				it->_key = L"installer\\" + it->_key;
				++it;
			}

			// 合并Installer剩余项
			softData2List.splice(softData2List.end(), installer);

			// 4.修正数据
			end = softData2List.end();
			CSensitivePaths sensiPaths;
			for(SoftData2Iter it = softData2List.begin(); it != end; ++it)
			{
				it->_mask = SDM_Key;

				// 修正主目录
				if(!it->_mainPath.empty())
				{
					it->_mask |= SDM_Main_Path;
					it->_mainPath = MakeAbsolutePath(it->_mainPath);
					trim_right(it->_mainPath, L"\\");
					make_lower(it->_mainPath);
					continue;
				}

				if(!it->_uninstString.empty())
				{
					// 排除msiexec
					if(StrStrIW(it->_uninstString.c_str(), L"msiexec") == NULL)
					{
						wstring path;
						if(!filesystem_traits<wchar_t>::file_exists(it->_uninstString.c_str()))
						{
							commandline_parser_w parser(it->_uninstString.c_str());
							if( parser.size() > 1)
							{
								if( filesystem_traits<wchar_t>::file_exists( parser[1] )||
									filesystem_traits<wchar_t>::is_directory( parser[1] ))
								{
									path = parser[1];
								}
								else
								{
									if(parser.size() != 0) 
										path = parser[0];
								}
							}
							else
							{
								if(parser.size() != 0) 
									path = parser[0];
							}
						}
						else
						{
							path = it->_uninstString;
						}

						if( !filesystem_traits<wchar_t>::is_directory( path.c_str() ) )
						{
							wstring::size_type last = path.find_last_of(L'\\');
							if(last != wstring::npos) path = path.substr(0, last);
						}

						if(filesystem_traits<wchar_t>::is_directory(path.c_str()))
						{
							path = MakeAbsolutePath(path);
							trim_right(path, L"\\");
							make_lower(path);

							if(!sensiPaths.IsSensitive(path))
							{
								it->_mask |= SDM_Main_Path;
								it->_mainPath = path;
								continue;
							}
						}
					}
				}

				if(!it->_iconLocation.empty())
				{
					wstring::size_type pos = it->_iconLocation.find_last_of(L'\\');
					if(pos != wstring::npos)
					{
						wstring path = it->_iconLocation.substr(0, pos);
						if(filesystem_traits<wchar_t>::is_directory(path.c_str()))
						{
							path = MakeAbsolutePath(path);
							trim_right(path, L"\\");
							make_lower(path);

							if(!sensiPaths.IsSensitive(path))
							{
								it->_mask |= SDM_Main_Path;
								it->_mainPath = path;
								continue;
							}
						}
					}
				}
			}
		}

		//
		// 搜索64位软件
		//
		if(Is64BitWindows())
		{
			CFsRedDisScoped fsRedDisabled;

			// 仅搜索Uninstall
			SoftData2List uninstall;
			SearchUninstall(uninstall, KEY_READ | KEY_WOW64_64KEY);

			// 修正数据
			CSensitivePaths sensiPaths;
			SoftData2Iter end = uninstall.end();
			for(SoftData2Iter it = uninstall.begin(); it != end; ++it)
			{
				it->_mask = SDM_Key;
				it->_key = wstring(L"x64\\uninstall\\") + it->_key;

				if(!it->_mainPath.empty())
				{
					it->_mask |= SDM_Main_Path;
					it->_mainPath = MakeAbsolutePath(it->_mainPath);
					trim_right(it->_mainPath, L"\\");
					make_lower(it->_mainPath);
					continue;
				}

				if(!it->_uninstString.empty())
				{
					// 排除msiexec
					if(StrStrIW(it->_uninstString.c_str(), L"msiexec") == NULL)
					{
						wstring path;
						if(!filesystem_traits<wchar_t>::file_exists(it->_uninstString.c_str()))
						{
							commandline_parser_w parser(it->_uninstString.c_str());
							if( parser.size() > 1)
							{
								if( filesystem_traits<wchar_t>::file_exists( parser[1] )||
									filesystem_traits<wchar_t>::is_directory( parser[1] ))
								{
									path = parser[1];
								}
								else
								{
									if(parser.size() != 0) 
										path = parser[0];
								}
							}
							else
							{
								if(parser.size() != 0) 
									path = parser[0];
							}
						}
						else
						{
							path = it->_uninstString;
						}

						if( !filesystem_traits<wchar_t>::is_directory( path.c_str() ) )
						{
							wstring::size_type last = path.find_last_of(L'\\');
							if(last != wstring::npos) path = path.substr(0, last);
						}

						if(filesystem_traits<wchar_t>::is_directory(path.c_str()))
						{
							path = MakeAbsolutePath(path);
							trim_right(path, L"\\");
							make_lower(path);

							if(!sensiPaths.IsSensitive(path))
							{
								it->_mask |= SDM_Main_Path;
								it->_mainPath = path;
								continue;
							}
						}
					}
				}

				if(!it->_iconLocation.empty())
				{
					wstring::size_type pos = it->_iconLocation.find_last_of(L'\\');
					if(pos != wstring::npos)
					{
						wstring path = it->_iconLocation.substr(0, pos);
						if(filesystem_traits<wchar_t>::is_directory(path.c_str()))
						{
							path = MakeAbsolutePath(path);
							trim_right(path, L"\\");
							make_lower(path);

							if(!sensiPaths.IsSensitive(path))
							{
								it->_mask |= SDM_Main_Path;
								it->_mainPath = path;
								continue;
							}
						}
					}
				}
			}

			// 以名称为Key过滤掉混在32位软件中的64位软件
			FilterSoftData2ByName(uninstall, softData2List);

			// 合并结果
			softData2List.splice(softData2List.end(), uninstall);
		}

		//_softData2List.sort(SoftData2DefSort());
	}
	catch(...) {}
}

void CSearchSoftData::FilterSoftData2ByName(const SoftData2List &filter, SoftData2List &src)
{
	SoftData2Iter end = src.end();
	for(SoftData2Iter it = src.begin(); it != end; )
	{
		SoftData2CIter it2 = find_if(filter.begin(), filter.end(), SoftData2NameFind(it->_displayName));
		if(it2 == filter.end())
		{
			++it;
		}
		else
		{
			src.erase(it++);
		}
	}
}

void CSearchSoftData::SearchInstaller(SoftData2List &softData2List, REGSAM samDesired)
{
	try
	{
		const wstring root = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\UserData";

		KeyList keyList;
		RegListKey(HKEY_LOCAL_MACHINE, root, keyList, samDesired);

		KeyCIter end = keyList.end();
		for(KeyCIter it = keyList.begin(); it != end; ++it)
		{
			KeyList keyList2;
			wstring root2 = root + L'\\' + *it + L"\\Products";
			RegListKey(HKEY_LOCAL_MACHINE, root2, keyList2, samDesired);

			KeyCIter end2 = keyList2.end();
			for(KeyCIter it2 = keyList2.begin(); it2 != end2; ++it2)
			{
				SoftData2 softData2;
				if(!Hex2Guid(*it2, softData2._guid)) continue;

				if(LoadInstallerData(root2 + L'\\' + *it2 + L"\\InstallProperties", softData2, samDesired))
				{
					// 此处以Key为Key
					softData2._key = to_lower(*it2);
					softData2List.push_back(softData2);
				}
			}
		}

		// 以Key除重
		softData2List.erase(unordered_unique(softData2List.begin(), softData2List.end(), SoftData2KeyCmp()), softData2List.end());
	}
	catch(...) {}
}

BOOL CSearchSoftData::LoadInstallerData(const std::wstring &root, SoftData2 &softData2, REGSAM samDesired)
{
	try
	{
		reg_key_w key(HKEY_LOCAL_MACHINE, root.c_str(), samDesired);

		if(
			(key.has_value(L"ParentKeyName")	/*&& wcscmp(key.get_value(L"ParentKeyName").value_sz().c_str(), L"OperatingSystem") == 0*/) ||
			(key.has_value(L"SystemComponent") && key.get_value(L"SystemComponent").value_dword() == 1) ||
			(key.has_value(L"ReleaseType")		&& _wcsicmp(key.get_value(L"ReleaseType").value_sz().c_str(), L"Hotfix") == 0) ||
			(key.has_value(L"IsMinorUpgrade")	&& key.get_value(L"IsMinorUpgrade").value_dword() == 1)
			)
			return FALSE;

		if(key.has_value(L"DisplayName"))
		{
			softData2._displayName = key.get_value(L"DisplayName").value_sz();
		}

		if(key.has_value(L"UninstallString"))
		{
			softData2._uninstString = key.get_value(L"UninstallString").value_expand_sz();
		}

		if(key.has_value(L"InstallLocation"))
		{
			softData2._mainPath = key.get_value(L"InstallLocation").value_expand_sz();
		}

		if(key.has_value(L"DisplayIcon"))
		{
			softData2._iconLocation = key.get_value(L"DisplayIcon").value_expand_sz();
		}

		if(key.has_value(L"EstimatedSize"))
		{
			softData2._size = key.get_value(L"EstimatedSize").value_dword()*1024;
		}

		if(key.has_value(L"Comments"))
		{
			softData2._descriptReg = key.get_value(L"Comments").value_sz();
		}

		// 忽略所有显示名称与卸载字符串为空的情况
		if(softData2._displayName.empty() || softData2._uninstString.empty())
		{
			return FALSE;
		}

		return TRUE;
	}
	catch(...) {}
	return FALSE;
}

void CSearchSoftData::SearchUninstall(SoftData2List &softData2List, REGSAM samDesired)
{
	try
	{
		const wstring root = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

		KeyList keyList;
		RegListKey(HKEY_LOCAL_MACHINE, root, keyList, samDesired);

		KeyCIter end = keyList.end();
		for(KeyCIter it = keyList.begin(); it != end; ++it)
		{
			SoftData2 softData2;
			if(IsGuid(*it))
			{
				softData2._guid = to_lower(*it);
			}
			else
			{
				Hex2Guid(*it, softData2._guid);
			}

			if(LoadUninstallData(HKEY_LOCAL_MACHINE, root + L'\\' + *it, softData2, samDesired))
			{
				// 此处以Key为Key
				softData2._key = to_lower(*it);
				softData2List.push_back(softData2);
			}
		}

		//
		// 64系统不检测HEKY_CURRENT_USER
		//
		if((samDesired & KEY_WOW64_64KEY) == 0)
		{
			keyList.clear();
			RegListKey(HKEY_CURRENT_USER, root, keyList, samDesired);

			end = keyList.end();
			for(KeyCIter it = keyList.begin(); it != end; ++it)
			{
				SoftData2 softData2;
				if(IsGuid(*it))
				{
					softData2._guid = to_lower(*it);
				}
				else
				{
					Hex2Guid(*it, softData2._guid);
				}

				if(LoadUninstallData(HKEY_CURRENT_USER, root + L'\\' + *it, softData2, samDesired))
				{
					// 此处以Key为Key
					softData2._key = to_lower(*it);
					softData2List.push_back(softData2);
				}
			}

			// 以Key除重
			softData2List.erase(unordered_unique(softData2List.begin(), softData2List.end(), SoftData2KeyCmp()), softData2List.end());
		}
	}
	catch(...) {}
}

BOOL CSearchSoftData::LoadUninstallData(HKEY hKey, const std::wstring &root, SoftData2 &softData2, REGSAM samDesired)
{
	try
	{
		reg_key_w key(hKey, root.c_str(), samDesired);

		if(
			(key.has_value(L"ParentKeyName")	/*&& wcscmp(key.get_value(L"ParentKeyName").value_sz().c_str(), L"OperatingSystem") == 0*/) ||
			(key.has_value(L"SystemComponent") && key.get_value(L"SystemComponent").value_dword() == 1) ||
			(key.has_value(L"ReleaseType")		&& _wcsicmp(key.get_value(L"ReleaseType").value_sz().c_str(), L"Hotfix") == 0) ||
			(key.has_value(L"IsMinorUpgrade")	&& key.get_value(L"IsMinorUpgrade").value_dword() == 1)
			)
		{
			if(!softData2._guid.empty()) 
			{
				if((samDesired & KEY_WOW64_64KEY) == KEY_WOW64_64KEY)
					_excluded64.insert(softData2._guid);
				else
					_excluded.insert(softData2._guid);
			}
			return FALSE;
		}

		if(key.has_value(L"DisplayName"))
		{
			softData2._displayName = key.get_value(L"DisplayName").value_sz();
		}

		if(key.has_value(L"UninstallString"))
		{
			softData2._uninstString = key.get_value(L"UninstallString").value_expand_sz();
		}

		if(softData2._uninstString.empty() && !softData2._guid.empty())
		{
			softData2._uninstString = L"MsiExec.exe /X";
			softData2._uninstString += softData2._guid;
		}

		if(key.has_value(L"InstallLocation"))
		{
			softData2._mainPath = key.get_value(L"InstallLocation").value_expand_sz();

			if(!filesystem_traits<wchar_t>::file_exists(softData2._mainPath.c_str()))
				softData2._mainPath.clear();
		}

		if(key.has_value(L"DisplayIcon"))
		{
			softData2._iconLocation = key.get_value(L"DisplayIcon").value_expand_sz();
		}

		if(key.has_value(L"EstimatedSize"))
		{
			softData2._size = key.get_value(L"EstimatedSize").value_dword()*1024;
		}

		if(key.has_value(L"Comments"))
		{
			softData2._descriptReg = key.get_value(L"Comments").value_sz();
		}

		// 忽略所有显示名称与卸载字符串为空的情况
		if(softData2._displayName.empty() || softData2._uninstString.empty())
		{
			return FALSE;
		}

		return TRUE;
	}
	catch(...) {}

	return FALSE;
}

void CSearchSoftData::MergeSoftData2(SoftData2 &uninst, const SoftData2 &inst)
{
	if(uninst._displayName.empty() && !inst._displayName.empty())
		uninst._displayName = inst._displayName;

	if(uninst._uninstString.empty() && !inst._uninstString.empty())
		uninst._uninstString = inst._uninstString;

	if(uninst._mainPath.empty() && !inst._mainPath.empty())
		uninst._mainPath = inst._mainPath;

	if(uninst._iconLocation.empty() && !inst._iconLocation.empty())
		uninst._iconLocation = inst._iconLocation;

	if(uninst._size == 0 && inst._size != 0)
		uninst._size = inst._size;

	if(uninst._descriptReg.empty() && !inst._descriptReg.empty())
		uninst._descriptReg = inst._descriptReg;
}

std::wstring MakeAbsolutePath(LPCWSTR pPath)
{
	wstring absPath;
	DWORD ret = ::GetLongPathNameW(pPath, NULL, 0);
	if(ret != 0)
	{
		auto_buffer<wchar_t> buffer(ret + 1);
		ret = ::GetLongPathNameW(pPath, &buffer[0], static_cast<DWORD>(buffer.size()));
		if(ret > 0 && ret < static_cast<DWORD>(buffer.size()))
		{
			absPath = &buffer[0];
		}
	}

	return (absPath.empty() ? pPath : absPath);
}

std::wstring ExpandEnvironString(LPCWSTR pPath)
{
	wstring expPath;
	DWORD ret = ::ExpandEnvironmentStringsW(pPath, NULL, 0);
	if(ret != 0)
	{
		auto_buffer<wchar_t> buffer(ret + 1);
		ret = ::ExpandEnvironmentStringsW(pPath, &buffer[0], static_cast<DWORD>(buffer.size()));
		if(ret > 0 && ret < static_cast<DWORD>(buffer.size()))
		{
			expPath = &buffer[0];
		}
	}

	return (expPath.empty() ? pPath : expPath);
}

BOOL FixSoftData2AndSave(CSoftUninstall *pSoftUninst, SoftData2List &softData2List)
{
	ISQLiteComDatabase3 *pDB = pSoftUninst->GetDBPtr();
	CCacheTransaction transaction(pDB);

	// Sqlite大小写敏感，所以使用like匹配
	static const LPCWSTR pSqlQuery = 
		L"select * from soft_info_list where "
		L"(match_type=1 and pattern = ?) or (match_type=0 and (? like pattern));";

	CComPtr<ISQLiteComStatement3> pStateQuery;
	pDB->PrepareStatement(pSqlQuery, &pStateQuery);

	static const LPCWSTR pSqlInsert = 
		L"insert into local_soft_list"
		L"(soft_key,"
		L"guid,"
		L"display_name,"
		L"main_path,"
		L"descript,"
		L"descript_reg,"
		L"info_url,"
		L"spell_whole,"
		L"spell_acronym,"
		L"icon_location,"
		L"uninstall_string,"
		L"logo_url,"
		L"size,"
		L"last_use,"
		L"type_id,"
		L"soft_id,"
		L"match_type,"
		L"pattern,"
		L"daycnt)"
		L"values(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";

	CComPtr<ISQLiteComStatement3> pStateInsert;
	pDB->PrepareStatement(pSqlInsert, &pStateInsert);

	SoftData2Iter end = softData2List.end();
	for(SoftData2Iter it = softData2List.begin(); it != end; ++it)
	{
		it->_mask |= SDM_Key | SDM_PinYin;
		pSoftUninst->GetPinYin(it->_displayName, it->_spellWhole, it->_spellAcronym);

		// 查询类型
		CComPtr<ISQLiteComResultSet3> pRs;

		wstring displayName = to_lower(it->_displayName);
		pStateQuery->Bind(1, displayName.c_str());
		pStateQuery->Bind(2, displayName.c_str());

		int matchType = 0;
		LPCWSTR pPattern = NULL;

		HRESULT hr = pStateQuery->ExecuteQuery(&pRs);
		if(SUCCEEDED(hr))
		{
			CheckSoftType(pRs, *it, matchType, pPattern);
		}


		ISQLiteComStatement3* pState = pStateInsert;
		if( pState )
		{
			// 插入新记录
			BindHelper(pStateInsert, 1, it->_key);
			BindHelper(pStateInsert, 2, it->_guid);
			BindHelper(pStateInsert, 3, it->_displayName);
			BindHelper(pStateInsert, 4, it->_mainPath);
			BindHelper(pStateInsert, 5, it->_descript);
			BindHelper(pStateInsert, 6, it->_descriptReg);
			BindHelper(pStateInsert, 7, it->_infoUrl);
			BindHelper(pStateInsert, 8, it->_spellWhole);
			BindHelper(pStateInsert, 9, it->_spellAcronym);
			BindHelper(pStateInsert, 10, it->_iconLocation);
			BindHelper(pStateInsert, 11, it->_uninstString);
			BindHelper(pStateInsert, 12, it->_logoUrl);
			pStateInsert->Bind(13, it->_size);
			pStateInsert->Bind(14, static_cast<int>(it->_lastUse));
			pStateInsert->Bind(15, static_cast<int>(it->_type));
			pStateInsert->Bind(16, static_cast<int>(it->_id));
			pStateInsert->Bind(17, matchType);
			BindHelper(pStateInsert, 18, pPattern);
			pStateInsert->Bind(19, static_cast<int>(it->_count));

			pStateInsert->ExecuteUpdate();
			pStateInsert->Reset();
			pStateQuery->Reset();
		}
		else
		{
			ATLASSERT(FALSE);
		}
		
	}

	return TRUE; 
}

BOOL UpdateRegCacheFlag(Skylark::ISQLiteComDatabase3 *pDB)
{
	ULONGLONG nowInst, nowUninst1, nowUninst2;
	if(
		!GetKeyWriteTime(HKEY_LOCAL_MACHINE, L"software\\microsoft\\windows\\currentversion\\installer\\userdata", nowInst) ||
		!GetKeyWriteTime(HKEY_CURRENT_USER, L"software\\microsoft\\windows\\currentversion\\uninstall", nowUninst1) ||
		!GetKeyWriteTime(HKEY_LOCAL_MACHINE, L"software\\microsoft\\windows\\currentversion\\uninstall", nowUninst2)
		) 
		return FALSE;

	CCacheFlagOpr cache(pDB);
	cache.Insert(L"02:\\software\\microsoft\\windows\\currentversion\\installer\\userdata", nowInst);
	cache.Insert(L"01:\\software\\microsoft\\windows\\currentversion\\uninstall", nowUninst1);
	cache.Insert(L"02:\\software\\microsoft\\windows\\currentversion\\uninstall", nowUninst2);
	return TRUE;
}

BOOL GetKeyWriteTime(HKEY hKey, LPCWSTR pSub, ULONGLONG &writeTime)
{
	CRegKey reg;
	LONG res = reg.Open(hKey, pSub, KEY_READ);
	if(res != ERROR_SUCCESS) return FALSE;

	FILETIME ftWriteTime;
	res = ::RegQueryInfoKeyW(hKey, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ftWriteTime);

	ULARGE_INTEGER larger;
	larger.LowPart = ftWriteTime.dwLowDateTime;
	larger.HighPart = ftWriteTime.dwHighDateTime;

	writeTime = larger.QuadPart;
	return TRUE;
}

BOOL DeleteFile2(const std::wstring &path, BOOL recycle)
{
	if(recycle)
	{
		wstring pathex(path.size() + 2, L'\0');
		pathex.assign(path);

		SHFILEOPSTRUCTW fp = {0};

		fp.pFrom = pathex.c_str();
		fp.wFunc = FO_DELETE;
		fp.fFlags= FOF_NO_UI | FOF_NOERRORUI | FOF_SILENT | FOF_NOCONFIRMATION | FOF_ALLOWUNDO;

		return (0 == ::SHFileOperationW(&fp));
	}
	else
	{
		return ::DeleteFileW(path.c_str());
	}
}

bool LinkFileEnum::operator()(const std::wstring &root, const WIN32_FIND_DATAW &wfd)
{
	if((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
	{
		LPWSTR pExt = ::PathFindExtensionW(wfd.cFileName);
		if(pExt != NULL && _wcsicmp(pExt, L".lnk") == 0)
		{
			wstring path = root + L'\\' + wfd.cFileName;
			_fileList.push_back(make_lower(path));
		}
	}
	return true;
}

BOOL CheckSoftType(ISQLiteComResultSet3 *pRs, SoftData2 &softData2, int &matchType, LPCWSTR &pPattern)
{
	matchType = 0;
	pPattern = NULL;
	if(pRs->IsEof()) return FALSE;

	size_t lenPattern = 0;
	softData2._mask	|= SDM_Description | SDM_Info_Url | SDM_Type;

	do 
	{
		matchType = pRs->GetInt(L"match_type");

		// 全匹配优先
		if(matchType == 1) 
		{
			softData2._id		= static_cast<LONG>(pRs->GetInt(L"soft_id"));
			softData2._type		= static_cast<LONG>(pRs->GetInt(L"type_id"));
			softData2._descript	= pRs->GetAsString(L"brief");
			softData2._infoUrl	= pRs->GetAsString(L"info_url");
			softData2._logoUrl	= pRs->GetAsString(L"logo_url");
			pPattern			= pRs->GetAsString(L"pattern");
			break;
		}
		else
		{
			//
			// 最长模式匹配
			//
			//@Sample
			//
			// 搜索字符串为：金山安全卫士 v1.5
			// 命中模式分别为：金山%、金山安全%、金山安全卫士%
			//
			// 根据“最长模式匹配”原则，则最终匹配模式为：金山安全卫士%
			//
			LPCWSTR pCurPattern = pRs->GetAsString(L"pattern");
			if(pCurPattern != NULL)
			{
				size_t len = wcslen(pCurPattern);
				if(len > lenPattern)
				{
					softData2._id		= static_cast<LONG>(pRs->GetInt(L"soft_id"));
					softData2._type		= static_cast<LONG>(pRs->GetInt(L"type_id"));
					softData2._descript	= pRs->GetAsString(L"brief");
					softData2._infoUrl	= pRs->GetAsString(L"info_url");
					softData2._logoUrl	= pRs->GetAsString(L"logo_url");
					pPattern			= pCurPattern;

					lenPattern			= len;
				}
			}
		}

		pRs->NextRow();
	}
	while(!pRs->IsEof());

	return (pPattern != NULL);
}

BOOL CLinkOpr::Initialize()
{
	HRESULT hr = co_create_instance(CLSID_ShellLink, &_pShellLink);
	if(!SUCCEEDED(hr)) return FALSE;

	CComPtr<IPersistFile> pPersistFile;
	hr = _pShellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&_pPersistFile));
	if(!SUCCEEDED(hr)) return FALSE;

	return TRUE;
}

BOOL CLinkOpr::GetPath(LPCWSTR pLinkPath, LPWSTR pBuf, size_t, PWIN32_FIND_DATAW pWfd /* = NULL */)
{
	if(_pShellLink == NULL || _pPersistFile == NULL) return FALSE;

	HRESULT hr = _pPersistFile->Load(pLinkPath, STGM_READ);
	if(!SUCCEEDED(hr)) return FALSE;

	hr = _pShellLink->GetPath(pBuf, MAX_PATH, pWfd, SLGP_UNCPRIORITY);
	if(!SUCCEEDED(hr)) return FALSE;

	return TRUE;
}

BOOL Is64BitWindows()
{
#if defined(_WIN64)
	return TRUE; // 64-bit programs run only on Win64
#elif defined(_WIN32)
	// 32-bit programs run on both 32-bit and 64-bit Windows
	// so must sniff
	static BOOL checked = FALSE;
	static BOOL is64Bit = FALSE;

	if(!checked)
	{
		typedef BOOL (WINAPI *LPFN_ISWOW64PROCESS)(HANDLE, PBOOL);

		LPFN_ISWOW64PROCESS fnIsWow64Process = (LPFN_ISWOW64PROCESS)
			::GetProcAddress(::GetModuleHandle(L"kernel32"), "IsWow64Process");
		if (NULL != fnIsWow64Process)
		{
			fnIsWow64Process(::GetCurrentProcess(),&is64Bit);
		}

		checked = TRUE;
	}

	return is64Bit;
#else
	return FALSE; // Win64 does not support Win16
#endif
}

void CFsRedDisScoped::Startup()
{
	if(!Is64BitWindows()) return;

	HMODULE hMod = ::GetModuleHandleW(L"kernel32.dll");
	if(hMod == NULL) return;

	spfnWow64DisableWow64FsRedirection = (PFN_Wow64DisableWow64FsRedirection)
		::GetProcAddress(hMod, "Wow64DisableWow64FsRedirection");

	spfnWow64RevertWow64FsRedirection = (PFN_Wow64RevertWow64FsRedirection)
		::GetProcAddress(hMod, "Wow64RevertWow64FsRedirection");
}

void CFsRedDisScoped::Shutdown()
{
	// do nothing
}

CFsRedDisScoped::CFsRedDisScoped()
{
	if(spfnWow64DisableWow64FsRedirection == NULL) return;

	pOldValue = NULL;
	spfnWow64DisableWow64FsRedirection(&pOldValue);
}

CFsRedDisScoped::~CFsRedDisScoped()
{
	if(spfnWow64RevertWow64FsRedirection == NULL || pOldValue == NULL) return;

	spfnWow64RevertWow64FsRedirection(pOldValue);
}

void Cx64Api::Startup()
{
	HMODULE hMod = ::LoadLibraryW(L"Advapi32.dll");
	if(hMod == NULL) return;

	sRegDeleteKeyExW = (PFN_RegDeleteKeyExW)
		::GetProcAddress(hMod, "RegDeleteKeyExW");
}

LONG Cx64Api::RegDeleteKeyExW(HKEY hKey, LPCWSTR lpSubKey, REGSAM samDesired, DWORD Reserved)
{
	if(sRegDeleteKeyExW == NULL)
	{
		return ::RegDeleteKeyW(hKey, lpSubKey);
	}
	else
	{
		return sRegDeleteKeyExW(hKey, lpSubKey, samDesired, Reserved);
	}
}

void Cx64Api::Shutdown()
{
	// do nothing
}

BOOL Is64BitKey(const std::wstring &key)
{
	return (wcsncmp(key.c_str(), L"x64\\", 4) == 0);
}

BOOL IsUninstallKey(const std::wstring &key)
{
	int offset = 0;
	if(Is64BitKey(key)) offset = 4;

	return (wcsncmp(&key[offset], L"uninstall\\", 10) == 0);
}

BOOL GetNameKey(const std::wstring &key, std::wstring &name)
{
	wstring::size_type pos = key.find_last_of(L'\\');
	if(pos == wstring::npos) return FALSE;

	name = key.substr(pos + 1);
	return TRUE;
}

BOOL PathFileExistsX64(std::wstring &path)
{
	if(::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) 
	{
		return TRUE;
	}

	if(Is64BitWindows())
	{
		//
		// 将32位路径转为64位路径
		//

		// " (x86)\\"
		LPWSTR pStr = StrStrIW(&path[0], L" (x86)\\");
		if(pStr != NULL)
		{
			wcscpy(pStr, pStr + 6);
			path.resize(path.size() - 6);

			return (::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES);
		}

		// "SysWOW64"
		pStr = StrStrIW(&path[0], L"\\SysWOW64\\");
		if(pStr != NULL)
		{
			memcpy(pStr + 1, L"System32", 8 * 2);
			path.resize(path.size() - 9);

			return (::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES);
		}
	}

	return FALSE;
}

}