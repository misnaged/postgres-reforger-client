[BaseContainerProps()]
class EDF_WebProxyConnectionInfoBase : EDF_DbConnectionInfoBase
{
	[Attribute(defvalue: "localhost", desc: "Web proxy hostname.")]
	string m_sProxyHost;

	[Attribute(defvalue: "8080", desc: "Web proxy port.")]
	int m_iProxyPort;

	[Attribute(desc: "Use TLS/SSL to connect to the web proxy.")]
	bool m_bSecureConnection;

	[Attribute(desc: "Custom headers for all requests e.g. api keys.")]
	ref array<ref EDF_WebProxyParameter> m_aHeaders;

	[Attribute(desc: "Additional parameters added to the url with ...&key=value")]
	ref array<ref EDF_WebProxyParameter> m_aParameters;

	//------------------------------------------------------------------------------------------------
	override void ReadOptions(string connectionString)
	{
		super.ReadOptions(connectionString);

		if (m_sDatabaseName.Length() == connectionString.Length())
			return; // No other params

		if (!m_aHeaders)
			m_aHeaders = {};

		if (!m_aParameters)
			m_aParameters = {};

		array<string> keyValuePairs();
		int paramsStart = m_sDatabaseName.Length() + 1;
		connectionString.Substring(paramsStart, connectionString.Length() - paramsStart).Split("&", keyValuePairs, true);
		foreach (string keyValuePair : keyValuePairs)
		{
			int keyIdx = keyValuePair.IndexOf("=");
			if (keyIdx == -1)
				continue;

			string key = keyValuePair.Substring(0, keyIdx).Trim();
			string keyLower = key;
			keyLower.ToLower();

			int valueFrom = keyIdx + 1;
			string value = keyValuePair.Substring(valueFrom, keyValuePair.Length() - valueFrom).Trim();
			string valueLower = value;
			valueLower.ToLower();

			switch (keyLower)
			{
				case "host":
				case "proxyhost":
				{
					m_sProxyHost = value;
					break;
				}

				case "port":
				case "proxyport":
				{
					m_iProxyPort = value.ToInt(8001);
					break;
				}

				case "ssl":
				case "tls":
				case "secure":
				case "secureconnection":
				{
					m_bSecureConnection = valueLower == "1" || valueLower == "true" || valueLower == "yes";
					break;
				}
				
				case "headers":
				case "parameters":
				{
					array<string> kvs();
					value.Split(",", kvs, true);
					if ((kvs.Count() % 2) != 0)
					{
						Debug.Error(string.Format("Invalid '%1' connection info parameter. Not all keys have a value!", key));
						break;
					}
					
					bool isHeaders = keyLower == "headers";
					
					for (int nKey = 0, count = kvs.Count() - 1; nKey < count; nKey+=2)
					{
						auto param = new EDF_WebProxyParameter(kvs.Get(nKey), kvs.Get(nKey + 1));

						if (isHeaders)
						{
							m_aHeaders.Insert(param);
							continue;
						}
						
						m_aParameters.Insert(param);
					}
					break;
				}
				
				default: 
				{
					// Backwards compatiblity, will remove it at some point
					m_aParameters.Insert(new EDF_WebProxyParameter(key, value));

					//Debug.Error(string.Format("Unknown parameter '%1'='%2' in connection info.", key, value));
				}
			}
		}
	}
}

sealed class EDF_CustomDefaultTitle : BaseContainerCustomTitleField
{
	protected string m_sDefaultTitle;

	//------------------------------------------------------------------------------------------------
	override bool _WB_GetCustomTitle(BaseContainer source, out string title)
	{
		super._WB_GetCustomTitle(source, title);
		if (!title)
			title = m_sDefaultTitle;

		return true;
	}

	//------------------------------------------------------------------------------------------------
	void EDF_CustomDefaultTitle(string propertyName, string defaultTitle)
	{
		m_PropertyName = propertyName;
		m_sDefaultTitle = defaultTitle;
	}
}

[EDF_CustomDefaultTitle("m_sKey", "UNCONFIGURED"), BaseContainerProps()]
sealed class EDF_WebProxyParameter
{
	[Attribute()]
	string m_sKey;

	[Attribute()]
	string m_sValue;

	//------------------------------------------------------------------------------------------------
	void EDF_WebProxyParameter(string key, string value)
	{
		// Only use ctor params if they provide something. Otherwise it was set via attribute already.
		if (key)
			m_sKey = key;
		
		if (value)
			m_sValue = value;
	}
}

sealed class EDF_WebProxyDbDriverCallback : RestCallback
{
	protected static ref set<ref EDF_WebProxyDbDriverCallback> s_aSelfReferences = new set<ref EDF_WebProxyDbDriverCallback>();

	protected ref EDF_DbOperationCallback m_pCallback;
	protected typename m_tResultType;

	protected string m_sVerb;
	protected string m_sUrl;

	//------------------------------------------------------------------------------------------------
	override void OnSuccess(string data, int dataSize)
	{
		#ifdef PERSISTENCE_DEBUG
		Print(string.Format("%1::OnSuccess(%2, %3) from %4:%5", this, dataSize, data, m_sVerb, m_sUrl), LogLevel.VERBOSE);
		#endif

		s_aSelfReferences.RemoveItem(this);

		auto statusCallback = EDF_DbOperationStatusOnlyCallback.Cast(m_pCallback);
		if (statusCallback)
		{
			statusCallback.Invoke(EDF_EDbOperationStatusCode.SUCCESS);
			return;
		}

		auto findCallback = EDF_DbFindCallbackBase.Cast(m_pCallback);
		if (!findCallback)
			return; // Could have been a status only operation but no callback was set

		if (dataSize == 0)
		{
			OnFailure(EDF_EDbOperationStatusCode.FAILURE_RESPONSE_MALFORMED);
			return;
		}

		SCR_JsonLoadContext reader();
		array<ref EDF_DbEntity> resultEntities();

		// Read per line individually until json load context has polymorph support: https://feedback.bistudio.com/T173074
		
		if (EDF_DbName.Get(m_tResultType) == "Item")
		{
		array<string> lines();
		data.Split("_DIVIDER", lines, true);
		PrintFormat("lines are: %1", lines.Count());
		foreach(string l : lines)
		{
			EDF_DbEntity entity = EDF_DbEntity.Cast(m_tResultType.Spawn());

			if (!reader.ImportFromString(l) || !reader.ReadValue("", entity))
			{
				OnFailure(EDF_EDbOperationStatusCode.FAILURE_RESPONSE_MALFORMED);
				return;
			}

			resultEntities.Insert(entity);
		}
		} else {
		
			EDF_DbEntity entity = EDF_DbEntity.Cast(m_tResultType.Spawn());

			if (!reader.ImportFromString(data) || !reader.ReadValue("", entity))
			{
				OnFailure(EDF_EDbOperationStatusCode.FAILURE_RESPONSE_MALFORMED);
				return;
			}

			resultEntities.Insert(entity);
		}

		


		findCallback.Invoke(EDF_EDbOperationStatusCode.SUCCESS, resultEntities);
	};

	//------------------------------------------------------------------------------------------------
	override void OnError(int errorCode)
	{
		s_aSelfReferences.RemoveItem(this);

		#ifdef PERSISTENCE_DEBUG
		Print(string.Format("%1::OnError(%2) from %3:%4", this, typename.EnumToString(ERestResult, errorCode), m_sVerb, m_sUrl), LogLevel.ERROR);
		#endif

		EDF_EDbOperationStatusCode statusCode;
		switch (errorCode)
		{
			default:
			{
				statusCode = EDF_EDbOperationStatusCode.FAILURE_UNKNOWN;
				break;
			}
		}

		OnFailure(statusCode);
	}

	//------------------------------------------------------------------------------------------------
	override void OnTimeout()
	{
		s_aSelfReferences.RemoveItem(this);

		#ifdef PERSISTENCE_DEBUG
		Print(string.Format("%1::OnTimeout() from %2:%3", this, m_sVerb, m_sUrl), LogLevel.VERBOSE);
		#endif

		OnFailure(EDF_EDbOperationStatusCode.FAILURE_DB_UNAVAILABLE);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnFailure(EDF_EDbOperationStatusCode statusCode)
	{
		auto statusCallback = EDF_DbOperationStatusOnlyCallback.Cast(m_pCallback);
		if (statusCallback)
		{
			statusCallback.Invoke(statusCode);
			return;
		}

		auto findCallback = EDF_DbFindCallbackBase.Cast(m_pCallback);
		if (findCallback)
			findCallback.Invoke(EDF_EDbOperationStatusCode.FAILURE_UNKNOWN, new array<ref EDF_DbEntity>);
	};

	//------------------------------------------------------------------------------------------------
	static void Reset()
	{
		s_aSelfReferences = new set<ref EDF_WebProxyDbDriverCallback>();
	}

	//------------------------------------------------------------------------------------------------
	void EDF_WebProxyDbDriverCallback(EDF_DbOperationCallback callback, typename resultType = typename.Empty, string verb = string.Empty, string url = string.Empty)
	{
		m_pCallback = callback;
		m_tResultType = resultType;
		m_sVerb = verb;
		m_sUrl = url;
		s_aSelfReferences.Insert(this);
	};
}

sealed class EDF_WebProxyDbDriverFindRequest
{
	EDF_DbFindCondition m_pCondition;
	ref array<ref TStringArray> m_aOrderBy;
	int m_iLimit;
	int m_iOffset;

	//------------------------------------------------------------------------------------------------
	protected bool SerializationSave(BaseSerializationSaveContext saveContext)
	{
		if (m_pCondition)
			saveContext.WriteValue("condition", m_pCondition);

		if (m_aOrderBy)
			saveContext.WriteValue("orderBy", m_aOrderBy);

		if (m_iLimit != -1)
			saveContext.WriteValue("limit", m_iLimit);

		if (m_iOffset != -1)
			saveContext.WriteValue("offset", m_iOffset);

		return true;
	}

	//------------------------------------------------------------------------------------------------
	void EDF_WebProxyDbDriverFindRequest(EDF_DbFindCondition condition, array<ref TStringArray> orderBy, int limit, int offset)
	{
		m_pCondition = condition;
		m_aOrderBy = orderBy;
		m_iLimit = limit;
		m_iOffset = offset;
	}
}

class EDF_WebProxyDbDriver : EDF_DbDriver
{
	protected RestContext m_pContext;
	protected string m_sAddtionalParams;

	//------------------------------------------------------------------------------------------------
	override bool Initialize(notnull EDF_DbConnectionInfoBase connectionInfo)
	{
		EDF_WebProxyConnectionInfoBase webConnectInfo = EDF_WebProxyConnectionInfoBase.Cast(connectionInfo);
		string url = "http";

		if (webConnectInfo.m_bSecureConnection)
			url += "s";

		string commas = ":/";
		url += string.Format("%1/%2:%3", commas, webConnectInfo.m_sProxyHost, webConnectInfo.m_iProxyPort);
		m_pContext = GetGame().GetRestApi().GetContext(url);
 		string headers;
		bool hasContentType, hasUserAgent;
		if (webConnectInfo.m_aHeaders)
		{
			foreach (EDF_WebProxyParameter header : webConnectInfo.m_aHeaders)
			{
				if (!hasContentType && (header.m_sKey.Compare("Content-Type", false) == 0))
					hasContentType = true;
				
				if (!hasUserAgent && (header.m_sKey.Compare("User-Agent", false) == 0))
					hasUserAgent = true;
				
				if (!headers.IsEmpty())
					headers += ",";
				
				headers += string.Format("%1,%2", header.m_sKey, header.m_sValue);
			}
		}
		
		if (!hasContentType)
		{
			if (!headers.IsEmpty())
				headers += ",";

			headers += "Content-Type,application/json";
		}

		if (!hasUserAgent)
			headers += ",User-Agent,AR-EDF";

		m_pContext.SetHeaders(headers);
		
		if (webConnectInfo.m_aParameters)
		{
			int paramCount = webConnectInfo.m_aParameters.Count();
			if (paramCount > 0)
			{
				m_sAddtionalParams += "?";

				foreach (int idx, EDF_WebProxyParameter parameter : webConnectInfo.m_aParameters)
				{
					m_sAddtionalParams += string.Format("%1=%2", parameter.m_sKey, parameter.m_sValue);
					if (idx < paramCount - 1)
						m_sAddtionalParams += "&";
				}
			}
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override EDF_EDbOperationStatusCode AddOrUpdate(notnull EDF_DbEntity entity)
	{
		typename entityType = entity.Type();
		string request = string.Format("/%1/update", EDF_DbName.Get(entityType));
		string data = Serialize(entity);
		m_pContext.POST_now(request, data);
		return EDF_EDbOperationStatusCode.SUCCESS;
	}

	//------------------------------------------------------------------------------------------------
	override EDF_EDbOperationStatusCode Remove(typename entityType, string entityId)
	{
 		string request;
		if (EDF_DbName.Get(entityType) == "Character")
		{
		 request = string.Format("/%1/delete?char_uuid=%2", EDF_DbName.Get(entityType), entityId);
		} else{
		   request = string.Format("/%1/delete?uuid=%2", EDF_DbName.Get(entityType), entityId);
		}
		m_pContext.DELETE_now(request, string.Empty);
		return EDF_EDbOperationStatusCode.SUCCESS;
	}

	//------------------------------------------------------------------------------------------------
	override EDF_DbFindResultMultiple<EDF_DbEntity> FindAll(typename entityType, EDF_DbFindCondition condition = null, array<ref TStringArray> orderBy = null, int limit = -1, int offset = -1)
	{
		// Can not be implemented until https://feedback.bistudio.com/T166390 is fixed. Hopefully AR 0.9.9 or 0.9.10
		return new EDF_DbFindResultMultiple<EDF_DbEntity>(EDF_EDbOperationStatusCode.FAILURE_NOT_IMPLEMENTED, {});
	}

	//------------------------------------------------------------------------------------------------
	override void AddOrUpdateAsync(notnull EDF_DbEntity entity, EDF_DbOperationStatusOnlyCallback callback = null)
	{
		if (s_bForceBlocking)
		{
			EDF_EDbOperationStatusCode statusCode = AddOrUpdate(entity);
			if (callback)
				callback.Invoke(statusCode);

			return;
		}

		typename entityType = entity.Type();
		string request = string.Format("/%1/update", EDF_DbName.Get(entityType));
		string data = Serialize(entity);
		m_pContext.POST(new EDF_WebProxyDbDriverCallback(callback, verb: "POST", url: request), request, data);
	}

	//------------------------------------------------------------------------------------------------
	override void RemoveAsync(typename entityType, string entityId, EDF_DbOperationStatusOnlyCallback callback = null)
	{
		if (s_bForceBlocking)
		{
			EDF_EDbOperationStatusCode statusCode = Remove(entityType, entityId);
			if (callback)
				callback.Invoke(statusCode);

			return;
		}
		string request;
		if (EDF_DbName.Get(entityType) == "Character")
		{
		 request = string.Format("/%1/delete?char_uuid=%2", EDF_DbName.Get(entityType), entityId);
		} else{
		  request = string.Format("/%1/delete?uuid=%2", EDF_DbName.Get(entityType), entityId);
		}
		m_pContext.DELETE(new EDF_WebProxyDbDriverCallback(callback, verb: "DELETE", url: request), request, string.Empty);
	}

	//------------------------------------------------------------------------------------------------
	override void FindAllAsync(typename entityType, EDF_DbFindCondition condition = null, array<ref TStringArray> orderBy = null, int limit = -1, int offset = -1, EDF_DbFindCallbackBase callback = null)
	{
		/*
		EDF_DbFindFieldCondition findField = EDF_DbFindFieldCondition.Cast(condition);
		if (findField && findField.m_bUsesTypename)
		{
			Debug.Error("Typename usage in find conditions currently unsupported by this driver.");
			if (callback)
				callback.Invoke(EDF_EDbOperationStatusCode.FAILURE_NOT_IMPLEMENTED, new array<ref EDF_DbEntity>());
		}
		*/

		if (s_bForceBlocking)
		{
			EDF_DbFindResultMultiple<EDF_DbEntity> results = FindAll(entityType, condition, orderBy, limit, offset);
			if (callback)
				callback.Invoke(results.GetStatusCode(), results.GetEntities());

			return;
		}

		string request = string.Format("/%1", EDF_DbName.Get(entityType));
		string data = Serialize(new EDF_WebProxyDbDriverFindRequest(condition, orderBy, limit, offset));

		m_pContext.POST(new EDF_WebProxyDbDriverCallback(callback, entityType, verb: "POST", url: request), request, data);
	}

	//------------------------------------------------------------------------------------------------
	static string Serialize(Managed data)
	{
		ContainerSerializationSaveContext writer();
		JsonSaveContainer jsonContainer = new JsonSaveContainer();
		jsonContainer.SetMaxDecimalPlaces(5);
		writer.SetContainer(jsonContainer);
		writer.WriteValue("", data);
		return jsonContainer.ExportToString();
	}

	//------------------------------------------------------------------------------------------------
	//! TODO: Rely on https://feedback.bistudio.com/T173074 instead once added
	static string MoveTypeDiscriminatorIn(string data, string discriminator = "_type")
	{
		string quoutedDiscriminator = string.Format("\"%1\"", discriminator);
		int typeDiscriminatorIdx = data.IndexOf(quoutedDiscriminator);
		if (typeDiscriminatorIdx == -1)
			return data;

		string processedString;
		int dataPosition;
		int dataLength = data.Length();

		while (true)
		{
			// Add content until discriminator
			processedString += data.Substring(dataPosition, typeDiscriminatorIdx - dataPosition);

			// Extract discriminator
			int discriminatorLength = data.IndexOfFrom(typeDiscriminatorIdx, ",") - typeDiscriminatorIdx + 1;
			string typeDiscrimiantor = data.Substring(typeDiscriminatorIdx, discriminatorLength);

			// Read remaining data until injection point
			int injectionIndex = data.IndexOfFrom(typeDiscriminatorIdx + discriminatorLength, "{") + 1;
			int skipOver = typeDiscriminatorIdx + discriminatorLength;
			processedString += data.Substring(skipOver, injectionIndex - skipOver);

			// Inject discriminator
			processedString += typeDiscrimiantor;

			dataPosition = injectionIndex;
			typeDiscriminatorIdx = data.IndexOfFrom(skipOver, quoutedDiscriminator);
			if (typeDiscriminatorIdx == -1)
			{
				processedString += data.Substring(dataPosition, dataLength - dataPosition);
				break;
			}
		}

		return processedString;
	}

	//------------------------------------------------------------------------------------------------
	static string MoveTypeDiscriminatorOut(string data, string discriminator = "_type")
	{
		string quoutedDiscriminator = string.Format("\"%1\"", discriminator);
		int typeDiscriminatorIdx = data.IndexOf(quoutedDiscriminator);
		if (typeDiscriminatorIdx == -1)
			return data;

		string processedString;
		int dataPosition;
		int dataLength = data.Length();

		while (true)
		{
			// Extract discriminator
			int discriminatorLength = data.IndexOfFrom(typeDiscriminatorIdx, ",") - typeDiscriminatorIdx + 1;
			string typeDiscrimiantor = data.Substring(typeDiscriminatorIdx, discriminatorLength);

			// Find injection position
			int injectAt = -1, remainingBrackes = 2;
			for (int nChar = typeDiscriminatorIdx - 1; nChar >= 0; nChar--)
			{
				if ((data.Get(nChar) == "\"") && (--remainingBrackes == 0))
				{
					injectAt = nChar;
					break;
				}
			}

			// Get data in front
			processedString += data.Substring(dataPosition, injectAt - dataPosition);

			// Inject type discriminator
			processedString += typeDiscrimiantor;

			// Add data from in between
			processedString += data.Substring(injectAt, typeDiscriminatorIdx - injectAt);

			// Continue behind original position
			dataPosition = typeDiscriminatorIdx + discriminatorLength;

			typeDiscriminatorIdx = data.IndexOfFrom(dataPosition, quoutedDiscriminator);
			if (typeDiscriminatorIdx == -1)
			{
				processedString += data.Substring(dataPosition, dataLength - dataPosition);
				break;
			}
		}

		return processedString;
	}

	//------------------------------------------------------------------------------------------------
	void ~EDF_WebProxyDbDriver()
	{
		if (!m_pContext)
			return;

		m_pContext.reset();
		m_pContext = null;
	}
}
