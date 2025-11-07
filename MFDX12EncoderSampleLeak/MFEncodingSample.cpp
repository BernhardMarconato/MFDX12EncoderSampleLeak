#include "pch.h"
#include "MFEncodingSample.h"
#include <iostream>

using namespace winrt;

void MFEncodingSample::Log(hstring const& msg)
{
	OutputDebugStringW(msg.c_str());
	OutputDebugStringW(L"\n");
	std::wcout << msg.c_str() << L"\n";
}

void MFEncodingSample::RunRepro()
{
	try
	{
		// Force disable NVIDIA encoder to increase chance of getting DX12 encoder
		m_forceDisallowNvidiaEncoder = true;

		InitializeMFObjects();
		CreateSourceReader();

		// First pass with SinkWriter
		CreateSinkWriter(L"output1.mp4");
		Log(L"\n=== Recording first pass (5 seconds) ===");
		ReadSamplesWithSinkWriter(5);
		Log(L"\n=== First pass done ===");
		RemoveSinkWriter();

		// Second pass with new SinkWriter
		Log(L"\n=== Creating second SinkWriter instance ===");
		CreateSinkWriter(L"output2.mp4");
		Log(L"\n=== Recording second pass (20 seconds) ===");
		ReadSamplesWithSinkWriter(20);
		Log(L"\n=== Second pass done ===");
		RemoveSinkWriter();

		Log(L"\n=== REPRO COMPLETE ===");
	}
	catch (wil::ResultException const& e)
	{
		std::wstring ws;
		ws.assign(e.what(), e.what() + strlen(e.what()));
		Log(L"\nException: " + hstring(ws));
	}
	catch (hresult_error const& e)
	{
		Log(L"\nException: " + e.message());
	}
}

void MFEncodingSample::InitializeMFObjects()
{
	THROW_IF_FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL));

	com_ptr<ID3D11Device> device;
	D3D_FEATURE_LEVEL lvl;
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
	THROW_IF_FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, device.put(), &lvl, nullptr));

	THROW_IF_FAILED(MFCreateDXGIDeviceManager(&m_resetToken, m_dxgiManager.put()));
	THROW_IF_FAILED(m_dxgiManager->ResetDevice(device.get(), m_resetToken));
}

static com_ptr<IMFActivate> GetFirstVideoCaptureDevice()
{
	com_ptr<IMFAttributes> attrs;
	THROW_IF_FAILED(MFCreateAttributes(attrs.put(), 1));
	THROW_IF_FAILED(attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));

	IMFActivate** devs = nullptr;
	UINT32 count = 0;
	THROW_IF_FAILED(MFEnumDeviceSources(attrs.get(), &devs, &count));

	com_array<com_ptr<IMFActivate>> arr{ devs, count, take_ownership_from_abi };
	if (count == 0)
	{
		THROW_HR(MF_E_NO_CAPTURE_DEVICES_AVAILABLE);
	}
	return arr[0];
}

void MFEncodingSample::CreateSourceReader()
{
	m_sourceReader = nullptr;
	m_mediaSource = nullptr;

	auto activate = GetFirstVideoCaptureDevice();
	wil::unique_cotaskmem_string name;
	UINT32 cch = 0;
	LOG_IF_FAILED(activate->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &cch));
	THROW_IF_FAILED(activate->ActivateObject(IID_PPV_ARGS(m_mediaSource.put())));

	if (name)
	{
		Log(L"Using device: " + hstring{ name.get() });
	}

	com_ptr<IMFAttributes> readerAttrs;
	THROW_IF_FAILED(MFCreateAttributes(readerAttrs.put(), 5));
	THROW_IF_FAILED(readerAttrs->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, m_dxgiManager.get()));
	THROW_IF_FAILED(readerAttrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
	THROW_IF_FAILED(readerAttrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE));
	THROW_IF_FAILED(readerAttrs->SetUINT32(MF_LOW_LATENCY, TRUE));

	com_ptr<IMFSourceReader> baseReader;
	THROW_IF_FAILED(MFCreateSourceReaderFromMediaSource(m_mediaSource.get(), readerAttrs.get(), baseReader.put()));
	m_sourceReader = baseReader.as<IMFSourceReaderEx>();

	// Select NV12 native type
	DWORD idx = 0;
	for (;;)
	{
		com_ptr<IMFMediaType> t;
		HRESULT hr = m_sourceReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, idx, t.put());
		if (hr == MF_E_NO_MORE_TYPES)
		{
			THROW_HR(MF_E_NOT_FOUND);
		}
		THROW_IF_FAILED(hr);

		GUID st{};
		THROW_IF_FAILED(t->GetGUID(MF_MT_SUBTYPE, &st));
		if (st == MFVideoFormat_NV12)
		{
			DWORD fl;
			THROW_IF_FAILED(m_sourceReader->SetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, t.get(), &fl));
			break;
		}
		++idx;
	}
}

void MFEncodingSample::CreateSinkWriter(const wchar_t* filename)
{
	m_sinkWriter = nullptr;
	m_sinkStreamIndex = 0;

	// Build full path to temp directory
	wchar_t tempPath[MAX_PATH];
	GetTempPathW(MAX_PATH, tempPath);
	std::wstring fullPath = std::wstring(tempPath) + filename;

	Log(L"Creating SinkWriter for: " + hstring(fullPath));

	// Get camera type for dimensions
	com_ptr<IMFMediaType> currentCameraType;
	THROW_IF_FAILED(m_sourceReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, currentCameraType.put()));

	UINT32 w = 0, h = 0;
	if (FAILED(MFGetAttributeSize(currentCameraType.get(), MF_MT_FRAME_SIZE, &w, &h)))
	{
		w = 640;
		h = 480;
	}

	UINT32 frNum = 30, frDen = 1;
	MFGetAttributeRatio(currentCameraType.get(), MF_MT_FRAME_RATE, &frNum, &frDen);

	// Create sink writer attributes
	com_ptr<IMFAttributes> sinkAttrs;
	THROW_IF_FAILED(MFCreateAttributes(sinkAttrs.put(), 4));
	THROW_IF_FAILED(sinkAttrs->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, m_dxgiManager.get()));
	THROW_IF_FAILED(sinkAttrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
	THROW_IF_FAILED(sinkAttrs->SetUINT32(MF_LOW_LATENCY, FALSE));

	// Create sink writer
	com_ptr<IMFSinkWriter> sinkWriter;
	THROW_IF_FAILED(MFCreateSinkWriterFromURL(fullPath.c_str(), nullptr, sinkAttrs.get(), sinkWriter.put()));
	m_sinkWriter = sinkWriter.as<IMFSinkWriterEx>();

	// Configure output media type (H.264)
	com_ptr<IMFMediaType> outType;
	THROW_IF_FAILED(MFCreateMediaType(outType.put()));
	THROW_IF_FAILED(outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	THROW_IF_FAILED(outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
	THROW_IF_FAILED(outType->SetUINT32(MF_MT_AVG_BITRATE, 8000000));
	THROW_IF_FAILED(outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
	THROW_IF_FAILED(outType->SetUINT32(MF_MT_VIDEO_PROFILE, eAVEncH264VProfile_Main));
	THROW_IF_FAILED(outType->SetUINT32(MF_MT_VIDEO_LEVEL, 61));
	MFSetAttributeSize(outType.get(), MF_MT_FRAME_SIZE, w, h);
	MFSetAttributeRatio(outType.get(), MF_MT_FRAME_RATE, frNum, frDen);
	MFSetAttributeRatio(outType.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

	// Encoding attributes
	com_ptr<IMFAttributes> encodingAttributes;
	THROW_IF_FAILED(MFCreateAttributes(encodingAttributes.put(), 2));
	if (m_forceDisallowNvidiaEncoder)
	{
		// We cannot enforce an encoder with SinkWriter
		// But the NVIDIA encoder (nvEncMFTH264x) has a bug where it throws an error when setting CODECAPI_AVEncMaxFrameRate > 120
		// Which we can use - SinkWriter will then fall back to the next best encoder, usually AVC DX12 encoder
		MFSetAttributeRatio(encodingAttributes.get(), CODECAPI_AVEncMaxFrameRate, 240, 1);
	}

	// Add stream to sink writer
	THROW_IF_FAILED(m_sinkWriter->AddStream(outType.get(), &m_sinkStreamIndex));

	// Set input media type - use the actual output format from source reader
	THROW_IF_FAILED(m_sinkWriter->SetInputMediaType(m_sinkStreamIndex, currentCameraType.get(), encodingAttributes.get()));

	// Verify which encoder is being used
	com_ptr<IMFTransform> transform;
	HRESULT hr = m_sinkWriter->GetTransformForStream(m_sinkStreamIndex, 0, nullptr, transform.put());
	if (SUCCEEDED(hr) && transform)
	{
		// Try to get the friendly name from the transform
		com_ptr<IMFAttributes> transformAttrs;
		THROW_IF_FAILED(transform->GetAttributes(transformAttrs.put()));
		wil::unique_cotaskmem_string name;
		UINT32 cch = 0;
		THROW_IF_FAILED(transformAttrs->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &cch));
		Log(L"Encoder being used: " + hstring{ name.get() });
	}
	else
	{
		Log(L"Could not get encoder transform from SinkWriter");
	}

	// Begin writing
	THROW_IF_FAILED(m_sinkWriter->BeginWriting());

	Log(L"SinkWriter created successfully");
}

void MFEncodingSample::RemoveSinkWriter()
{
	if (m_sinkWriter)
	{
		m_sinkWriter->Finalize();
		m_sinkWriter = nullptr;
		Log(L"SinkWriter finalized and removed");
	}
}

void MFEncodingSample::ReadSamplesWithSinkWriter(int seconds)
{
	const DWORD stream = (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM;
	const LONGLONG target = seconds * 10ll * 1000ll * 1000ll;
	LONGLONG start = 0;
	bool started = false;
	DWORD flags = 0;
	LONGLONG ts = 0;
	com_ptr<IMFSample> sample;
	UINT64 sampleCount = 0;
	uint16_t detailedLogLimit = 15;

	Log(L"Starting to read samples...");

	while (true)
	{
		// Detailed logging for first 15 samples
		if (sampleCount < detailedLogLimit)
		{
			Log(L"About to read sample #" + hstring(std::to_wstring(sampleCount + 1)));
		}

		sample = nullptr;
		HRESULT hrRead = m_sourceReader->ReadSample(stream, 0, nullptr, &flags, &ts, sample.put());
		if (FAILED(hrRead))
		{
			Log(L"ReadSample failed: 0x" + hstring(std::to_wstring(hrRead)));
			THROW_IF_FAILED(hrRead);
		}

		if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
		{
			THROW_HR(MF_E_END_OF_STREAM);
		}

		if (sample)
		{
			if (sampleCount < detailedLogLimit)
			{
				Log(L"Sample #" + hstring(std::to_wstring(sampleCount + 1)) + L" received, timestamp: " + hstring(std::to_wstring(ts)));
			}

			if (!started)
			{
				start = ts;
				started = true;
			}

			// Write sample to sink writer
			if (sampleCount < detailedLogLimit)
			{
				Log(L"About to write sample #" + hstring(std::to_wstring(sampleCount + 1)) + L" to SinkWriter");
			}

			THROW_IF_FAILED(m_sinkWriter->WriteSample(m_sinkStreamIndex, sample.get()));
			++sampleCount;

			if (sampleCount < detailedLogLimit)
			{
				Log(L"Sample #" + hstring(std::to_wstring(sampleCount)) + L" written successfully");
			}
			else if (sampleCount % 30 == 0)
			{
				Log(L"Progress: " + hstring(std::to_wstring(sampleCount)) + L" samples written");
			}

			if ((ts - start) >= target)
			{
				break;
			}
		}
		else
		{
			if (sampleCount < detailedLogLimit)
			{
				Log(L"No sample returned (null sample)");
			}
		}
	}

	Log(L"\nFinal stats - Samples written: " + hstring(std::to_wstring(sampleCount)));
}