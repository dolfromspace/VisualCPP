
#include <map>
#include "CriticalSectionMinder.h"
#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

// Class for handling timer events.
// This is a template class so any other class can define its own timer using this timer template.
template<class T>
class CTemplateTimer 
{
public:
	// This is a helper object to pass objects around the timers and timer-starters.
	template<class U>
	class CTimerExpireCallback
	{	
	public:

		// This object is to access the methods within the class object that started the timer.
		U* m_pClassPtr;
		
		// Additional object needed for timer processing
		void* m_pObjectPtr;

		// The timer id will be stored in the map. However, for better indexing, another copy is 
		// kept within this class object.
		UINT_PTR m_uipTimerID;

		~CTimerExpireCallback() {};

		// Callback typedefs to handle timer events.
		typedef void( U::*fnTimer )(CTimerExpireCallback<U>* pCallback);
		fnTimer pfnTimerExpired;
		
		virtual void operator()()
		{
			// Call the callback mapped to pfnTimerExpired.
			(m_pClassPtr->*pfnTimerExpired)(this);
		};

		UINT m_uiTimeoutValue;

		// Construct a callback object.
		CTimerExpireCallback( U* pClassPtr, fnTimer pTimerProc, void* pObjectPtr, UINT uiTimoutValue ) : m_pClassPtr(pClassPtr), 
																										 pfnTimerExpired(pTimerProc), 
																										 m_pObjectPtr(pObjectPtr), 
																										 m_uiTimeoutValue(uiTimoutValue){};

		// Sometimes we want to remove timers based on the value of the 
		// object. This is a function typedef for the class objects to assign
		// their own comparers.
		bool PerformComparison( void* pObjectA, void* pObjectB )
		{
			// This is of course, the class object has assigned its comparer.
			// When null, we return false.
			// This will hardly be the case since PerformComparison 
			// is done always after the comparer assignment.
			if( pfnCompareObjects )
				return (m_pClassPtr->*pfnCompareObjects)( pObjectA, pObjectB );
			else 
				return false;
		};

		typedef bool( U::*fnComparer )( void* pObjectA, void* pObjectB );
		fnComparer pfnCompareObjects;
	};

	~CTemplateTimer()
	{
		{
			// Enter Critical Section.
			CriticalSectionMinder minder( m_mapTimerCallbacksCS );

			// Search through the map to delete every callback object.
			for (std::map<UINT_PTR, CTimerExpireCallback<T>*>::iterator it = m_mapTimerCallbacks.begin(); it != m_mapTimerCallbacks.end(); ++it)
			{
				void* pObjectToDelete = it->second->m_pObjectPtr;
				delete pObjectToDelete, pObjectToDelete = NULL;
			}

			// Clear the callback map.
			m_mapTimerCallbacks.clear();
		}

		// Care to delete the critical section object.
		DeleteCriticalSection( &m_mapTimerCallbacksCS );
	};

	CTemplateTimer()
	{
		// Initilalise critical section for the timer map.
		InitializeCriticalSection( &m_mapTimerCallbacksCS );
	};

	UINT_PTR AddTimer( T* pClassPtr, typename CTimerExpireCallback<T>::fnTimer pTimerProc, typename CTimerExpireCallback<T>::fnComparer pComparer, void* pObjectPtr, UINT uiTimeoutValue )
	{
		// Enter Critical Section.
		CriticalSectionMinder minder( m_mapTimerCallbacksCS );

		for (std::map<UINT_PTR, CTimerExpireCallback<T>*>::iterator it = m_mapTimerCallbacks.begin(); it != m_mapTimerCallbacks.end(); ++it)
		{
			void* pObject = it->second->m_pObjectPtr;
			if( it->second->PerformComparison( pObject, pObjectPtr ) )
			{
				// If the corresponding timer exists, we just reset and return.
				ResetTimer(it->first);
				return NULL;
			}
		}

		// No existing timer for this entry. 
		// Create a new callback object.
		CTimerExpireCallback<T>* pTimerObj = new CTemplateTimer<T>::CTimerExpireCallback<T>( pClassPtr, pTimerProc, pObjectPtr, uiTimeoutValue );
		
		// Assign comparer.
		pTimerObj->pfnCompareObjects = pComparer;
		
		// Start the timer.
		UINT_PTR uipNewTimerId = SetTimer( NULL, 0, pTimerObj->m_uiTimeoutValue, TimerProc );
		pTimerObj->m_uipTimerID = uipNewTimerId;

		// Add the timer to the map using the id as the key
		m_mapTimerCallbacks[uipNewTimerId] = pTimerObj;

		return uipNewTimerId;
	};

	void ResetTimer( UINT_PTR uipTimerId )
	{
		// Invalid timer ID.
		if( uipTimerId == 0 )
			return; 

		// Enter Critical Section.
		CriticalSectionMinder minder( m_mapTimerCallbacksCS );
		
		// Search through the map to find the corresponding timer.
		std::map<UINT_PTR, CTimerExpireCallback<T>*>::iterator it;
		CTimerExpireCallback<T>* pCallbackToReset;
		if( (it = m_mapTimerCallbacks.find(uipTimerId)) != m_mapTimerCallbacks.end() )
		{
			pCallbackToReset = it->second;

			// Stop the timer.
			KillTimer(NULL, uipTimerId);

			// Remove the callback object from the map.
			m_mapTimerCallbacks.erase(it);

			// Set a new timer for running.
			UINT_PTR uipNewtimerId = SetTimer( NULL, 0, it->second->m_uiTimeoutValue, TimerProc );

			// Store the previous callback into the map with the renewed timer ID.
			m_mapTimerCallbacks[uipNewtimerId] = pCallbackToReset;

			// Set the object's indexing ID.
			pCallbackToReset->m_uipTimerID = uipNewtimerId;
		}
		else 
			// Timer does not exist. 
			// Something's wrong, and the timer must be stopped.
			KillTimer(NULL, uipTimerId);
	};

	void RemoveTimer( void *pObjectPtr )
	{
		// Enter Critical Section.
		CriticalSectionMinder minder( m_mapTimerCallbacksCS );
	
		// Search through the map with the string values and find the corresponding callback object.
		UINT_PTR uipTimerIdToRemove = 0;
		for (std::map<UINT_PTR, CTimerExpireCallback<T>*>::iterator it = m_mapTimerCallbacks.begin(); it != m_mapTimerCallbacks.end(); ++it)
		{
			void* pObject = it->second->m_pObjectPtr;
			// Perform Comparison.
			if( it->second->PerformComparison( pObject, pObjectPtr ) )
			{
				// The timer ID found.
				uipTimerIdToRemove = it->first;
				break;
			}
		}

		// If the timerID is valid, we do furthur removing procedure.
		if( uipTimerIdToRemove )
			RemoveTimer( uipTimerIdToRemove );
	};

	void RemoveTimer( UINT_PTR uipTimerId )
	{
		// Invalid timer ID.
		if( uipTimerId == 0 )
			return; 
	
		// Enter Critical Section.
		CriticalSectionMinder minder( m_mapTimerCallbacksCS );

		// Stop the timer.
		KillTimer(NULL, uipTimerId);
		
		// Search through the callback map and delete the correpsonding timer.
		std::map<UINT_PTR, CTimerExpireCallback<T>*>::iterator it;
		if( (it = m_mapTimerCallbacks.find(uipTimerId)) != m_mapTimerCallbacks.end() )
		{
			CTimerExpireCallback<T>* pCallbackToDelete = it->second;
			m_mapTimerCallbacks.erase(uipTimerId);

			if( pCallbackToDelete != NULL )
				delete pCallbackToDelete, pCallbackToDelete = NULL;
		}
	};

	static void CALLBACK TimerProc( HWND hwnd, UINT uiMsg, UINT_PTR uipTimerId, DWORD dwTime )
	{
		// Invalid timer ID.
		if( uipTimerId == 0 )
			return;

		// Enter Critical Section.
		CriticalSectionMinder minder( m_mapTimerCallbacksCS );
	
		// Search through the callback map and delete the correpsonding timer.
		std::map<UINT_PTR, CTimerExpireCallback<T>*>::iterator it;
		if( (it = m_mapTimerCallbacks.find( uipTimerId )) != m_mapTimerCallbacks.end() )
			it->second->operator()();
	}

	// Map for callback objects.
	static std::map<UINT_PTR, CTimerExpireCallback<T>*> m_mapTimerCallbacks;

	// and its critical section
	static CRITICAL_SECTION	m_mapTimerCallbacksCS;
};

// Map Definition for callback objects.
template<class T>
std::map<UINT_PTR, typename CTemplateTimer<T>::CTimerExpireCallback<T>*> CTemplateTimer<T>::m_mapTimerCallbacks;

// and definition for its critical section
template<class T>
CRITICAL_SECTION	CTemplateTimer<T>::m_mapTimerCallbacksCS;