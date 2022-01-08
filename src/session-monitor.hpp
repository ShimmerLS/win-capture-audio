#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

#include <thread>
#include <string>
#include <memory>
#include <unordered_map>
#include <set>

#include <wil/com.h>
#include <wrl/implements.h>

#include "common.hpp"

using namespace Microsoft::WRL;

namespace SessionEvents {
enum SessionEvents {
	Shutdown = WM_USER,
	DeviceAdded,
	DeviceRemoved,
	SessionAdded,
	SessionExpired,
};
}

class DeviceNotificationClient
	: public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase,
			      IMMNotificationClient> {
private:
	DWORD worker_tid;

public:
	void SetWorkerThreadId(DWORD tid) { worker_tid = tid; }

	STDMETHOD(OnDefaultDeviceChanged)
	(EDataFlow flow, ERole role, LPCWSTR device_id) { return S_OK; }

	STDMETHOD(OnDeviceAdded)(LPCWSTR device_id)
	{
		auto id = new std::wstring(device_id);
		PostThreadMessageA(worker_tid, SessionEvents::DeviceAdded,
				   reinterpret_cast<WPARAM>(id), NULL);
		return S_OK;
	}

	STDMETHOD(OnDeviceRemoved)(LPCWSTR device_id)
	{
		auto id = new std::wstring(device_id);
		PostThreadMessageA(worker_tid, SessionEvents::DeviceRemoved,
				   reinterpret_cast<WPARAM>(id), NULL);
		return S_OK;
	}

	STDMETHOD(OnDeviceStateChanged)(LPCWSTR device_id, DWORD new_state)
	{
		auto id = new std::wstring(device_id);
		UINT code = new_state == DEVICE_STATE_ACTIVE
				    ? SessionEvents::DeviceAdded
				    : SessionEvents::DeviceRemoved;

		PostThreadMessageA(worker_tid, code,
				   reinterpret_cast<WPARAM>(id), NULL);

		return S_OK;
	}

	STDMETHOD(OnPropertyValueChanged)
	(LPCWSTR device_id, const PROPERTYKEY key) { return S_OK; }
};

class SessionNotificationClient
	: public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase,
			      IAudioSessionNotification> {

private:
	DWORD worker_tid;

public:
	SessionNotificationClient(DWORD worker_tid) : worker_tid{worker_tid} {};

	STDMETHOD(OnSessionCreated)(IAudioSessionControl *session)
	{
		session->AddRef();
		PostThreadMessageA(worker_tid, SessionEvents::SessionAdded,
				   reinterpret_cast<WPARAM>(session), NULL);
		return S_OK;
	}
};

class SessionEventNotificationClient
	: public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase,
			      IAudioSessionEvents> {
private:
	DWORD worker_tid;
	std::wstring session_id;

public:
	SessionEventNotificationClient(DWORD worker_tid,
				       const std::wstring &session_id)
		: worker_tid{worker_tid}, session_id{session_id}
	{
	}

	STDMETHOD(OnChannelVolumeChanged)
	(DWORD channel_count, float *new_channel_volume_array,
	 DWORD changed_channel, LPCGUID event_context)
	{
		return S_OK;
	}

	STDMETHOD(OnDisplayNameChanged)
	(LPCWSTR new_display_name, LPCGUID event_context) { return S_OK; }

	STDMETHOD(OnGroupingParamChanged)
	(LPCGUID new_grouping_param, LPCGUID event_context) { return S_OK; }

	STDMETHOD(OnIconPathChanged)
	(LPCWSTR new_icon_path, LPCGUID event_context) { return S_OK; }

	STDMETHOD(OnSessionDisconnected)(AudioSessionDisconnectReason reason)
	{
		PostThreadMessageA(worker_tid, SessionEvents::SessionExpired,
				   reinterpret_cast<WPARAM>(&session_id), NULL);
		return S_OK;
	}

	STDMETHOD(OnSimpleVolumeChanged)
	(float new_volume, BOOL new_mute, LPCGUID event_context)
	{
		return S_OK;
	}

	STDMETHOD(OnStateChanged)(AudioSessionState new_state)
	{
		if (new_state == AudioSessionStateExpired) {
			PostThreadMessageA(
				worker_tid, SessionEvents::SessionExpired,
				reinterpret_cast<WPARAM>(&session_id), NULL);
		}

		return S_OK;
	}
};

class DeviceWatcher {
private:
	DWORD worker_tid;

	std::wstring device_id;
	wil::com_ptr<IMMDevice> device;

	wil::com_ptr<IAudioSessionManager2> manager2;
	wil::com_ptr<IAudioSessionEnumerator> enumerator;

	SessionNotificationClient session_notification_client;

public:
	DeviceWatcher(std::wstring device_id, wil::com_ptr<IMMDevice> device,
		      DWORD worker_tid);

	~DeviceWatcher();
};

class SessionWatcher {
private:
	wil::com_ptr<IAudioSessionControl> session_control;
	std::optional<SessionEventNotificationClient> notification_client;

	DWORD pid;
	std::wstring session_id;
	std::string executable;

public:
	SessionWatcher(
		DWORD worker_tid,
		const wil::com_ptr<IAudioSessionControl> &session_control);

	~SessionWatcher();

	wil::com_ptr<IAudioSessionControl> GetSessionControl()
	{
		return session_control;
	}

	wil::com_ptr<IAudioSessionControl2> GetSessionControl2()
	{
		return session_control.query<IAudioSessionControl2>();
	}

	DWORD GetPid() { return pid; }
	std::string GetExecutable() { return executable; }
};

class SessionMonitor {
private:
	DWORD client_tid;

	UINT client_session_added;
	UINT client_session_expired;

	std::thread worker_thread;
	DWORD worker_tid;
	wil::unique_event worker_ready{wil::EventOptions::ManualReset};

	DeviceNotificationClient device_notification_client;
	wil::com_ptr<IMMDeviceEnumerator> enumerator;

	std::unordered_map<std::wstring, DeviceWatcher> device_watchers;
	std::unordered_map<std::wstring, SessionWatcher> session_watchers;

	std::set<std::tuple<DWORD, std::string>> sessions;

	void Init();
	void UnInit();

	void AddDevice(MSG msg);
	void AddDevice(std::wstring id, wil::com_ptr<IMMDevice> device);

	void RemoveDevice(MSG msg);
	void RemoveDevice(std::wstring id);

	void AddSession(MSG msg);
	void RemoveSession(MSG msg);

	void Run();
	void SafeRun();

public:
	SessionMonitor(DWORD client_tid, UINT client_session_added,
		       UINT client_session_expired);
	~SessionMonitor();
};