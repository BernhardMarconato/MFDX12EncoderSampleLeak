#pragma once
class MFEncodingSample
{
public:
    void RunRepro();
private:
    void Log(winrt::hstring const& msg);
    void InitializeMFObjects();
    void CreateSourceReader();
    void CreateSinkWriter(const wchar_t* filename);
    void RemoveSinkWriter();
    void ReadSamplesWithSinkWriter(int seconds);

    winrt::com_ptr<IMFMediaSource> m_mediaSource;
    winrt::com_ptr<IMFSourceReaderEx> m_sourceReader;
    winrt::com_ptr<IMFDXGIDeviceManager> m_dxgiManager;
    winrt::com_ptr<IMFSinkWriterEx> m_sinkWriter;
    DWORD m_sinkStreamIndex{};
    UINT m_resetToken{};
    bool m_forceDisallowNvidiaEncoder{};
};

