class PG_UuidCallback : RestCallback
{
	string Data;
	//------------------------------------------------------------------------------------------------
	override void OnError(int errorCode)
	{
		Print("OnError()");
	}
	void SetData(string data)
	{
		Data = data;
	}
	//------------------------------------------------------------------------------------------------
	override void OnTimeout()
	{
		Print("OnTimeout()");
	}

	//------------------------------------------------------------------------------------------------
	override void OnSuccess(string data, int dataSize)
	{
		if (dataSize > 0)
		{
		SetData(data);
		}
	}
	
	//----------------------------------------------------------------------------------------------//
	string GetUuidFromJSON(string data)
	{
	   	SCR_JsonLoadContext reader();
		reader.ImportFromString(data);
		reader.ReadValue("uuid", data);
		return data;
	}
	

}

class PG_HttpElem
{

	int delaytime = 500;

	protected ref PG_UuidCallback m_PG_UuidCallback;
	string Data;
	bool IsTriggered;


	//------------------------------------------------------------------------------------------------
	void ExecuteRequest(string charName)
	{
		if (!IsTriggered)
		{	
			Print("not null!");
			GetGame().GetCallqueue().Remove(ExecuteRequest);
		} 
		
		string contextURL = "http://localhost:8080/";
		RestContext context = GetGame().GetRestApi().GetContext(contextURL);
		string req = string.Format("get_uuid?username=%1", charName);
		context.GET(m_PG_UuidCallback, req);
		if (m_PG_UuidCallback.Data != "")
		{
			IsTriggered = false;
			Data = m_PG_UuidCallback.Data;
		} else {
			IsTriggered = true;
		}
	}
	
	
	//----------------------------------------------------------------------------------------------//
	void Repeat(string charName)
	{

			if (!m_PG_UuidCallback)
			m_PG_UuidCallback = new PG_UuidCallback();
			
			IsTriggered = true;
			GetGame().GetCallqueue().CallLater(ExecuteRequest, delaytime, IsTriggered, charName);
		
	}
	



}
