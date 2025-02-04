// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include <shlwapi.h>
#include <Wincrypt.h>   // For CryptStringToBinary.
#include <thumbcache.h> // For IThumbnailProvider.
#include <wincodec.h>   // Windows Imaging Codecs
#include <msxml6.h>
#include <new>
#include <string>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "msxml6.lib")

// this thumbnail provider implements IInitializeWithFile to enable being hosted
// in an isolated process for robustness

HRESULT RenderToBitmap(const std::wstring& filePath, UINT width, UINT height, HBITMAP* outBitmap);


class CModelThumbProvider
	: public IInitializeWithFile
	, public IThumbnailProvider
{
public:
	CModelThumbProvider()
		: _cRef(1)
	{
	}

	virtual ~CModelThumbProvider()
	{
	}

	// IUnknown
	IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		static const QITAB qit[] =
		{
			QITABENT(CModelThumbProvider, IInitializeWithFile),
			QITABENT(CModelThumbProvider, IThumbnailProvider),
			{ 0 },
		};
		return QISearch(this, qit, riid, ppv);
	}

	IFACEMETHODIMP_(ULONG) AddRef()
	{
		return InterlockedIncrement(&_cRef);
	}

	IFACEMETHODIMP_(ULONG) Release()
	{
		ULONG cRef = InterlockedDecrement(&_cRef);
		if (!cRef)
		{
			delete this;
		}
		return cRef;
	}

	// IInitializeWithFile
	IFACEMETHODIMP Initialize(LPCWSTR pszFilePath, DWORD grfMode);

	// IThumbnailProvider
	IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha);

private:
	long _cRef;
	std::wstring m_FilePath;
};

HRESULT CModelThumbProvider_CreateInstance(REFIID riid, void** ppv)
{
	CModelThumbProvider* pNew = new (std::nothrow) CModelThumbProvider();
	HRESULT hr = pNew ? S_OK : E_OUTOFMEMORY;
	if (SUCCEEDED(hr))
	{
		hr = pNew->QueryInterface(riid, ppv);
		pNew->Release();
	}
	return hr;
}

// IInitializeWithFile
IFACEMETHODIMP CModelThumbProvider::Initialize(LPCWSTR pszFilePath, DWORD)
{
	if (pszFilePath == NULL || pszFilePath[0] == 0)
		return E_UNEXPECTED;

	m_FilePath = pszFilePath;

	return S_OK;
}

// IThumbnailProvider
IFACEMETHODIMP CModelThumbProvider::GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha)
{
	__try
	{
		if (cx < 64)
			cx = 64;

		HRESULT hr = RenderToBitmap(m_FilePath, cx, cx, phbmp);

		if (FAILED(hr))
			return hr;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return E_UNEXPECTED;
	}

	*pdwAlpha = WTSAT_ARGB;

	return S_OK;
}
