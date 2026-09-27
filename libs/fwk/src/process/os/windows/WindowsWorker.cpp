#include "WindowsWorker.h"
#include "WindowsSvc.h"
#include <jde/fwk/process/execution.h>
#include <thread>
//#include "../../Framework/source/coroutine/Coroutine.h"
//#include "../../Framework/source/threading/Mutex.h"

#define var const auto

namespace Jde::Windows{
	up<WindowsWorkerMain> WindowsWorkerMain::_pInstance;
	constexpr ELogTags _tags{ ELogTags::Threads };

	WindowsWorker::WindowsWorker( bool runOnMainThread )ι:
		_eventQueue{ ManualResetEvent() },
		_eventStop{ ManualResetEvent() },
		_pThread{ runOnMainThread ? nullptr : mu<std::jthread>([&](){Loop();}) }
	{}

	WindowsWorker::WindowsWorker( Event&& initial )ι:
		_eventQueue{ ManualResetEvent() },
		_eventStop{ ManualResetEvent() },
		_queue{ {initial} },
		_pThread{ mu<std::jthread>( [&](){Loop();}) }
	{}

	void WindowsWorker::Stop()ι
	{}

	α WindowsWorkerMain::Push( coroutine_handle<>&& h, HANDLE hEvent, bool close )ι->void{
		ASSERT( _pInstance );
		if( _pInstance ){
			_pInstance->_queue.push( {{move(h), close}, hEvent} );
			if( !::SetEvent(_pInstance->_eventQueue.get()) )
				ERR( "SetEvent returned false" );
		}
	}

	α WindowsWorker::SubPush( Event& e )ι->bool{
		lg _{ _lock };
		var set = !Stopped() && _queue.size()+_coroutines.size()<MaxEvents();
		if( set ){
			_queue.push( move(e) );
			if( !::SetEvent(_eventQueue.get()) )
				ERR( "SetEvent returned false" );
		}
		return set;
	}

	α WindowsWorker::AddWaitRoutine( Event&& e )ι->void{
		if( ((TimePoint)_stop)==TimePoint{} ){
			_coroutines.push_back( e );
			_objects.push_back( e.WindowsEvent );
		}
		else
			ERR( "Stopped can't add event." );
	}

	α WindowsWorker::HandleEvent( Event&& e )ι->void{
		ASSERT( _coroutines.size()<MaxEvents() );
		AddWaitRoutine( move(e) );
	}

	α WindowsWorkerMain::HandleEvent( Event&& e )ι->void{
		if( _coroutines.size()<MaxEvents() )
			AddWaitRoutine( move(e) );
		else
		{
			bool used = false;
			for( auto pp = _workerBuffers.begin(); !used && pp!=_workerBuffers.end(); ++pp )
				used = (*pp)->SubPush( e );
			if( !used )
				_workerBuffers.push_back( ms<WindowsWorker>(move(e)) );
		}
	}

	α WindowsWorkerMain::HandleWorkerEvent()ι->void{
		for( auto pp = _workerBuffers.begin(); pp!=_workerBuffers.end(); pp = (*pp)->Stopped() ? _workerBuffers.erase(pp) : next(pp) );
	}

	α WindowsWorker::Loop()ι->DWORD{
		PreLoop();
		DWORD waitResult;
		for( ;; ){
			TRACE( "WaitForMultipleObjects" );
			waitResult = ::WaitForMultipleObjects( (DWORD)_objects.size(), _objects.data(), FALSE, INFINITE );
			TRACE( "WaitForMultipleObjects - returned {}", waitResult );
			if( waitResult==1 ){//_eventStop
				if( !::ResetEvent(_objects[waitResult]) )
					ERR( "ResetEvent failed for event object" );
				break;
			}
			ASSERT( false );//not sure of use case here.
			if( waitResult<_coroutines.size() ){
				auto pCoroutine = _coroutines.begin() + waitResult;
				if( pCoroutine->CoEvent )
					Post( move(pCoroutine->CoEvent) );
				else
					ERR( "cohandle is empty!"sv );
				auto p = _objects.begin() + waitResult;
				if( pCoroutine->Close )
					::CloseHandle( *p );
				_objects.erase( p );
				_coroutines.erase( pCoroutine );
				if( !IsMainThread() )
				{
					lg _{ _lock };
					if( _queue.size()+_coroutines.size()==0 ){
						_stop = Clock::now();
						break;
					}
				}
			}
			else if( waitResult==_coroutines.size() ){
				if( !::ResetEvent(_objects[waitResult]) )
					ERR( "ResetEvent failed for event object" );
				while( _queue.size() ){
					auto e = move( _queue.front() );
					_queue.pop();
					HandleEvent( move(e) );
				}
			}
			else
			{
				bool handled = IsMainThread();
				var stop = waitResult==_coroutines.size()+1;
				if( handled )
				{
					if( stop )
						_stop = Clock::now();	//TODO deal with stop.
					else if( _eventWorker && waitResult==_coroutines.size()+2 )
						HandleWorkerEvent();
					else
						handled = false;
				}
				else if( stop )
					CRITICAL( "Exiting with {} coroutines waiting.", _coroutines.size() );
				if( !handled ){
					if( waitResult==WAIT_FAILED )
						CRITICAL( "WaitForMultipleObjects returned {}", ::GetLastError() );
					ASSERT_DESC( false, format("Unknown result:  {}, count={}", waitResult, _objects.size()) );
				}
			}
		}
		return waitResult==WAIT_FAILED ? ERROR_BAD_COMMAND : NO_ERROR;
	}
	WindowsWorkerMain::WindowsWorkerMain( bool runOnMainThread )ι:
		WindowsWorker{ runOnMainThread }
	{}

	α WindowsWorkerMain::Start( optional<bool> pService )ι->void{
		var runOnMainThread = pService.has_value();
		var service = runOnMainThread && *pService;
		if( !_pInstance )
			_pInstance = up<WindowsWorkerMain>( new WindowsWorkerMain{runOnMainThread} );//private constructor
		if( service )
			Service::ReportStatus( SERVICE_RUNNING, NO_ERROR, 0 );
		if( runOnMainThread )
		{
			var result = _pInstance->Loop();
			if( service )
				Service::ReportStatus( SERVICE_STOPPED, result, 0 );
		}
	}

	α WindowsWorkerMain::Stop( int exitCode )ι->void{
		if( _pInstance ){
			var tags = ELogTags::App | ELogTags::Shutdown;
			DBGT( tags, "({})Stopping", exitCode );
			Process::Shutdown( exitCode );
			DBGT( tags, "({})Shutdown Complete", exitCode );
			if( !::SetEvent(_pInstance->_eventStop.get()) )
				ERR( "SetEvent returned false" );
		}
		else
			WARN( "Stopping but no instance" );
	}
}
