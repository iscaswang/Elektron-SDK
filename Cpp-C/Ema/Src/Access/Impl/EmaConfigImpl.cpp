/*|-----------------------------------------------------------------------------
 *|            This source code is provided under the Apache 2.0 license      --
 *|  and is provided AS IS with no warranty or guarantee of fit for purpose.  --
 *|                See the project's LICENSE.md for details.                  --
 *|           Copyright Thomson Reuters 2015. All rights reserved.            --
 *|-----------------------------------------------------------------------------
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <stdlib.h>
#ifndef WIN32
#include <unistd.h>
#endif

#include <new>

#include "ActiveConfig.h"
#include "EmaConfigImpl.h"
#include "DefaultXML.h"
#include "DictionaryCallbackClient.h"
#include "ReqMsg.h"
#include "DataType.h"
#include "ReqMsgEncoder.h"
#include "RefreshMsg.h"
#include "RefreshMsgEncoder.h"
#include "ProgrammaticConfigure.h"

using namespace thomsonreuters::ema::access;

extern const EmaString& getDTypeAsString( DataType::DataTypeEnum dType );

EmaConfigBaseImpl::EmaConfigBaseImpl() :
	_pEmaConfig(new XMLnode("EmaConfig", 0, 0)),
	_pProgrammaticConfigure(0),
	_instanceNodeName()
{
	createNameToValueHashTable();

	EmaString tmp("EmaConfig.xml");
	OmmLoggerClient::Severity result(readXMLconfiguration(tmp));
	if (result == OmmLoggerClient::ErrorEnum || result == OmmLoggerClient::VerboseEnum)
	{
		EmaString errorMsg("failed to extract configuration from [");
		errorMsg.append(tmp).append("]");
		_pEmaConfig->appendErrorMessage(errorMsg, result);
	}
}

EmaConfigBaseImpl::~EmaConfigBaseImpl()
{
	delete _pEmaConfig;

	xmlCleanupParser();
}

void EmaConfigBaseImpl::clear()
{
	_instanceNodeName.clear();
}

const XMLnode* EmaConfigBaseImpl::getNode(const EmaString& itemToRetrieve) const
{
	if (itemToRetrieve.empty())
		return 0;

	EmaString remainingPart;
	EmaString currentPart;
	EmaString name;
	const char* nodeSeparator("|");
	const char* nameSeparator(".");

	Int32 pos(itemToRetrieve.find(nodeSeparator, 0));
	if (pos == -1)
		currentPart = itemToRetrieve;
	else
	{
		currentPart = itemToRetrieve.substr(0, pos);
		remainingPart = itemToRetrieve.substr(pos + 1, EmaString::npos);
	}

	Int32 dotPos(currentPart.find(nameSeparator, 0));
	if (dotPos != -1)
	{
		currentPart = currentPart.substr(0, dotPos);
		name = currentPart.substr(dotPos + 1, EmaString::npos);
	}

	xmlList<XMLnode>* children(_pEmaConfig->getChildren());
	if (children->empty())
		return 0;

	const XMLnode* node = children->getFirst();

	while (node)
	{
		bool match(false);

		if (name.empty())
			match = (node->name() == currentPart);
		else
		{
			if (node->name() == currentPart)
			{
				ConfigElementList* attributes = node->attributes();
				ConfigElement* attribute = attributes->first();
				while (attribute)
				{
					if (attribute->name() == "Name" &&
						*static_cast<XMLConfigElement<EmaString>*>(attribute)->value() == name)
					{
						match = true;
						break;
					}
					attribute = attributes->next(attribute);
				}
			}
		}

		if (match)
		{
			if (remainingPart.empty())
				return node;

			children = node->getChildren();
			if (!children)
				return 0;
			node = children->getFirst();

			pos = remainingPart.find(nodeSeparator, 0);
			if (pos == -1)
			{
				currentPart = remainingPart;
				remainingPart.clear();
			}
			else
			{
				currentPart = remainingPart.substr(0, pos);
				remainingPart = remainingPart.substr(pos + 1, EmaString::npos);
			}

			dotPos = currentPart.find(nameSeparator, 0);
			if (dotPos == -1)
				name.clear();
			else
			{
				name = currentPart.substr(dotPos + 1, EmaString::npos);
				currentPart = currentPart.substr(0, dotPos);
			}
		}
		else
			node = children->getNext(node);
	}
	return 0;
}

OmmLoggerClient::Severity EmaConfigBaseImpl::readXMLconfiguration(const EmaString& fileName)
{
#ifdef WIN32
	char* fileLocation = _getcwd(0, 0);
#else
	char* fileLocation = getcwd(0, 0);
#endif
	EmaString message("reading configuration file [");
	message.append(fileName).append("] from [").append(fileLocation).append("]");
	_pEmaConfig->appendErrorMessage(message, OmmLoggerClient::VerboseEnum);
	free(fileLocation);

#ifdef WIN32
	struct _stat statBuffer;
	int statResult(_stat(fileName, &statBuffer));
#else
	struct stat statBuffer;
	int statResult(stat(fileName.c_str(), &statBuffer));
#endif
	if (statResult)
	{
		EmaString errorMsg("error reading configuration file [");
		errorMsg.append(fileName).append("]; system error message [").append(strerror(errno)).append("]");
		_pEmaConfig->appendErrorMessage(errorMsg, OmmLoggerClient::VerboseEnum);
		return OmmLoggerClient::VerboseEnum;
	}
	if (!statBuffer.st_size)
	{
		EmaString errorMsg("error reading configuration file [");
		errorMsg.append(fileName).append("]; file is empty");
		_pEmaConfig->appendErrorMessage(errorMsg, OmmLoggerClient::ErrorEnum);
		return OmmLoggerClient::ErrorEnum;
	}
	FILE* fp;
	fp = fopen(fileName.c_str(), "r");
	if (!fp)
	{
		EmaString errorMsg("error reading configuration file [");
		errorMsg.append(fileName).append("]; could not open file; system error message [").append(strerror(errno)).append("]");
		_pEmaConfig->appendErrorMessage(errorMsg, OmmLoggerClient::ErrorEnum);
		return OmmLoggerClient::ErrorEnum;
	}

	char* xmlData = reinterpret_cast<char*>(malloc(statBuffer.st_size + 1));
	size_t bytesRead(fread(reinterpret_cast<void*>(xmlData), sizeof(char), statBuffer.st_size, fp));
	if (!bytesRead)
	{
		EmaString errorMsg("error reading configuration file [");
		errorMsg.append(fileName).append("]; fread failed; system error message [").append(strerror(errno)).append("]");
		_pEmaConfig->appendErrorMessage(errorMsg, OmmLoggerClient::ErrorEnum);
		free(xmlData);
		return OmmLoggerClient::ErrorEnum;
	}
	fclose(fp);
	xmlData[bytesRead] = 0;
	bool retVal(extractXMLdataFromCharBuffer(fileName, xmlData, static_cast<int>(bytesRead)));
	free(xmlData);
	return (retVal == true ? OmmLoggerClient::SuccessEnum : OmmLoggerClient::ErrorEnum);
}

bool EmaConfigBaseImpl::extractXMLdataFromCharBuffer(const EmaString& what, const char* xmlData, int length)
{
	LIBXML_TEST_VERSION

		EmaString note("extracting XML data from ");
	note.append(what);
	_pEmaConfig->appendErrorMessage(note, OmmLoggerClient::VerboseEnum);

	xmlDocPtr xmlDoc = xmlReadMemory(xmlData, length, NULL, "notnamed.xml", XML_PARSE_HUGE);
	if (xmlDoc == NULL)
	{
		EmaString errorMsg("extractXMLdataFromCharBuffer: xmlReadMemory failed while processing ");
		errorMsg.append(what);
		_pEmaConfig->appendErrorMessage(errorMsg, OmmLoggerClient::ErrorEnum);
		xmlFreeDoc(xmlDoc);
		return false;
	}

	xmlNodePtr _xmlNodePtr = xmlDocGetRootElement(xmlDoc);
	if (_xmlNodePtr == NULL)
	{
		EmaString errorMsg("extractXMLdataFromCharBuffer: xmlDocGetRootElement failed while processing ");
		errorMsg.append(what);
		_pEmaConfig->appendErrorMessage(errorMsg, OmmLoggerClient::ErrorEnum);
		xmlFreeDoc(xmlDoc);
		return false;
	}

	processXMLnodePtr(_pEmaConfig, _xmlNodePtr);
	_pEmaConfig->name(reinterpret_cast<const char*>(_xmlNodePtr->name));
	xmlFreeDoc(xmlDoc);
	return true;
}

void EmaConfigBaseImpl::processXMLnodePtr(XMLnode* theNode, const xmlNodePtr& nodePtr)
{
	// add attibutes
	if (nodePtr->properties)
	{
		xmlChar* value(0);
		for (xmlAttrPtr attrPtr = nodePtr->properties; attrPtr != NULL; attrPtr = attrPtr->next)
		{
			if (!xmlStrcmp(attrPtr->name, reinterpret_cast<const xmlChar*>("value")))
				value = xmlNodeListGetString(attrPtr->doc, attrPtr->children, 1);
			else
			{
				EmaString errorMsg("got unexpected name [");
				errorMsg.append(reinterpret_cast<const char*>(attrPtr->name)).append("] while processing XML data; ignored");
				theNode->appendErrorMessage(errorMsg, OmmLoggerClient::VerboseEnum);
			}
		}

		static EmaString errorMsg;
		ConfigElement* e(createConfigElement(reinterpret_cast<const char*>(nodePtr->name), theNode->parent(),
			reinterpret_cast<const char*>(value), errorMsg));
		if (e)
			theNode->addAttribute(e);
		else if (!errorMsg.empty())
		{
			theNode->appendErrorMessage(errorMsg, OmmLoggerClient::ErrorEnum);
			errorMsg.clear();
		}

		if (value)
			xmlFree(value);
	}

	for (xmlNodePtr childNodePtr = nodePtr->children; childNodePtr; childNodePtr = childNodePtr->next)
	{
		if (xmlIsBlankNode(childNodePtr))
			continue;
		if (childNodePtr->type == XML_COMMENT_NODE)
			continue;

		switch (childNodePtr->type)
		{
		case XML_TEXT_NODE:
		case XML_PI_NODE:
			break;
		case XML_ELEMENT_NODE:
		{
			XMLnode* child(new XMLnode(reinterpret_cast<const char*>(childNodePtr->name), theNode->level() + 1, theNode));
			static int instance(0);
			++instance;
			processXMLnodePtr(child, childNodePtr);
			if (child->errorCount())
			{
				theNode->errors().add(child->errors());
				child->errors().clear();
			}
			--instance;

			if (childNodePtr->properties && !childNodePtr->children)
			{
				theNode->appendAttributes(child->attributes(), true);
				delete child;
			}
			else if (!childNodePtr->properties && childNodePtr->children)
			{
				if (theNode->addChild(child))
				{
					delete child;
				}
			}
			else if (!childNodePtr->properties && !childNodePtr->children)
			{
				EmaString errorMsg("node [");
				errorMsg.append(reinterpret_cast<const char*>(childNodePtr->name)).append("has neither children nor attributes");
				theNode->appendErrorMessage(errorMsg, OmmLoggerClient::VerboseEnum);
			}
			else
			{
				EmaString errorMsg("node [");
				errorMsg.append(reinterpret_cast<const char*>(childNodePtr->name)).append("has both children and attributes; node was ignored");
				theNode->appendErrorMessage(errorMsg, OmmLoggerClient::ErrorEnum);
			}
			break;
		}
		default:
			EmaString errorMsg("childNodePtr has unhandled type [");
			errorMsg.append(childNodePtr->type).append("]");
			theNode->appendErrorMessage(errorMsg, OmmLoggerClient::VerboseEnum);
		}
	}
}

void EmaConfigBaseImpl::createNameToValueHashTable()
{
	for (int i = 0; i < sizeof AsciiValues / sizeof(EmaString); ++i)
		nameToValueHashTable.insert(AsciiValues[i], ConfigElement::ConfigElementTypeAscii);

	for (int i = 0; i < sizeof EnumeratedValues / sizeof(EmaString); ++i)
		nameToValueHashTable.insert(EnumeratedValues[i], ConfigElement::ConfigElementTypeEnum);

	for (int i = 0; i < sizeof Int64Values / sizeof(EmaString); ++i)
		nameToValueHashTable.insert(Int64Values[i], ConfigElement::ConfigElementTypeInt64);

	for (int i = 0; i < sizeof UInt64Values / sizeof(EmaString); ++i)
		nameToValueHashTable.insert(UInt64Values[i], ConfigElement::ConfigElementTypeUInt64);
}

ConfigElement* EmaConfigBaseImpl::createConfigElement(const char* name, XMLnode* parent, const char* value, EmaString& errorMsg)
{
	ConfigElement* e(0);
	ConfigElement::ConfigElementType* elementType = nameToValueHashTable.find(name);
	if (elementType == 0)
		errorMsg.append("unsupported configuration element [").append(name).append("]; element ignored");
	else switch (*elementType)
	{
	case ConfigElement::ConfigElementTypeAscii:
		e = new XMLConfigElement<EmaString>(name, parent, ConfigElement::ConfigElementTypeAscii, value);
		break;
	case ConfigElement::ConfigElementTypeEnum:
		e = convertEnum(name, parent, value, errorMsg);
		break;
	case ConfigElement::ConfigElementTypeInt64:
	{
		if (!validateConfigElement(value, ConfigElement::ConfigElementTypeInt64))
		{
			errorMsg.append("value [").append(value).append("] for config element [").append(name).append("] is not a signed integer; element ignored");
			break;
		}
		Int64 converted;
#ifdef WIN32
		converted = _strtoi64(value, 0, 0);
#else
		converted = strtoll(value, 0, 0);
#endif
		e = new XMLConfigElement<Int64>(name, parent, ConfigElement::ConfigElementTypeInt64, converted);
	}
	break;
	case ConfigElement::ConfigElementTypeUInt64:
	{
		if (!validateConfigElement(value, ConfigElement::ConfigElementTypeUInt64))
		{
			errorMsg.append("value [").append(value).append("] for config element [").append(name).append("] is not an unsigned integer; element ignored");
			break;
		}
		UInt64 converted;
#ifdef WIN32
		converted = _strtoui64(value, 0, 0);
#else
		converted = strtoull(value, 0, 0);
#endif
		e = new XMLConfigElement<UInt64>(name, parent, ConfigElement::ConfigElementTypeUInt64, converted);
	}
	break;
	default:
		errorMsg.append("config element [").append(name).append("] had unexpected elementType [").append(*elementType).append("; element ignored");
		break;
	}
	return e;
}

bool EmaConfigBaseImpl::validateConfigElement(const char* value, ConfigElement::ConfigElementType valueType) const
{
	if (valueType == ConfigElement::ConfigElementTypeInt64 || valueType == ConfigElement::ConfigElementTypeUInt64)
	{
		if (!strlen(value))
			return false;
		const char* p = value;
		if (valueType == ConfigElement::ConfigElementTypeInt64 && !isdigit(*p))
		{
			if (*p++ != '-')
				return false;
			if (!*p)
				return false;
		}
		for (; *p; ++p)
			if (!isdigit(*p))
				return false;
		return true;
	}

	return false;
}

ConfigElement* EmaConfigBaseImpl::convertEnum(const char* name, XMLnode* parent, const char* value, EmaString& errorMsg)
{
	EmaString enumValue(value);
	int colonPosition(enumValue.find("::"));
	if (colonPosition == -1)
	{
		errorMsg.append("configuration attribute [").append(name)
			.append("] has an invalid Enum value format [").append(value)
			.append("]; expected typename::value (e.g., OperationModel::ApiDispatch)");
		return 0;
	}

	EmaString enumType(enumValue.substr(0, colonPosition));
	enumValue = enumValue.substr(colonPosition + 2, enumValue.length() - colonPosition - 2);

	if (!strcmp(enumType, "LoggerSeverity"))
	{
		static struct
		{
			const char* configInput;
			OmmLoggerClient::Severity convertedValue;
		} converter[] =
		{
			{ "Verbose", OmmLoggerClient::VerboseEnum },
			{ "Success", OmmLoggerClient::SuccessEnum },
			{ "Warning", OmmLoggerClient::WarningEnum },
			{ "Error", OmmLoggerClient::ErrorEnum },
			{ "NoLogMsg", OmmLoggerClient::NoLogMsgEnum }
		};

		for (int i = 0; i < sizeof converter / sizeof converter[0]; ++i)
			if (!strcmp(converter[i].configInput, enumValue))
				return new XMLConfigElement<OmmLoggerClient::Severity>(name, parent, ConfigElement::ConfigElementTypeEnum, converter[i].convertedValue);
	}
	else if (!strcmp(enumType, "LoggerType"))
	{
		static struct
		{
			const char* configInput;
			OmmLoggerClient::LoggerType convertedValue;
		} converter[] =
		{
			{ "File", OmmLoggerClient::FileEnum },
			{ "Stdout", OmmLoggerClient::StdoutEnum },
		};
		for (int i = 0; i < sizeof converter / sizeof converter[0]; i++)
			if (!strcmp(converter[i].configInput, enumValue))
				return new XMLConfigElement<OmmLoggerClient::LoggerType>(name, parent, ConfigElement::ConfigElementTypeEnum, converter[i].convertedValue);
	}
	else if (!strcmp(enumType, "DictionaryType"))
	{
		static struct
		{
			const char* configInput;
			Dictionary::DictionaryType convertedValue;
		} converter[] =
		{
			{ "FileDictionary", Dictionary::FileDictionaryEnum },
			{ "ChannelDictionary", Dictionary::ChannelDictionaryEnum },
		};

		for (int i = 0; i < sizeof converter / sizeof converter[0]; i++)
			if (!strcmp(converter[i].configInput, enumValue))
				return new XMLConfigElement<Dictionary::DictionaryType>(name, parent, ConfigElement::ConfigElementTypeEnum, converter[i].convertedValue);
	}
	else if (!strcmp(enumType, "ChannelType"))
	{
		static struct
		{
			const char* configInput;
			RsslConnectionTypes convertedValue;
		} converter[] =
		{
			{ "RSSL_SOCKET", RSSL_CONN_TYPE_SOCKET },
			{ "RSSL_HTTP", RSSL_CONN_TYPE_HTTP },
			{ "RSSL_ENCRYPTED", RSSL_CONN_TYPE_ENCRYPTED },
			{ "RSSL_RELIABLE_MCAST", RSSL_CONN_TYPE_RELIABLE_MCAST },
		};

		for (int i = 0; i < sizeof converter / sizeof converter[0]; i++)
			if (!strcmp(converter[i].configInput, enumValue))
				return new XMLConfigElement<RsslConnectionTypes>(name, parent, ConfigElement::ConfigElementTypeEnum, converter[i].convertedValue);
	}
	else if (!strcmp(enumType, "ServerType"))
	{
		static struct
		{
			const char* configInput;
			RsslConnectionTypes convertedValue;
		} converter[] =
		{
			{ "RSSL_SOCKET", RSSL_CONN_TYPE_SOCKET },
		};

		for (int i = 0; i < sizeof converter / sizeof converter[0]; i++)
			if (!strcmp(converter[i].configInput, enumValue))
				return new XMLConfigElement<RsslConnectionTypes>(name, parent, ConfigElement::ConfigElementTypeEnum, converter[i].convertedValue);
	}
	else if (!strcmp(enumType, "CompressionType"))
	{
		static struct
		{
			const char* configInput;
			RsslCompTypes convertedValue;
		} converter[] =
		{
			{ "None", RSSL_COMP_NONE },
			{ "ZLib", RSSL_COMP_ZLIB },
			{ "LZ4", RSSL_COMP_LZ4 },
		};

		for (int i = 0; i < sizeof converter / sizeof converter[0]; i++)
			if (!strcmp(converter[i].configInput, enumValue))
				return new XMLConfigElement<RsslCompTypes>(name, parent, ConfigElement::ConfigElementTypeEnum, converter[i].convertedValue);
	}
	else if (!strcmp(enumType, "StreamState"))
	{
		static struct
		{
			const char* configInput;
			OmmState::StreamState convertedValue;
		} converter[] =
		{
			{ "Open", OmmState::OpenEnum },
			{ "NonStreaming", OmmState::NonStreamingEnum },
			{ "ClosedRecover", OmmState::ClosedRecoverEnum },
			{ "Closed", OmmState::ClosedEnum },
			{ "ClosedRedirected", OmmState::ClosedRedirectedEnum },
		};

		for (int i = 0; i < sizeof converter / sizeof converter[0]; i++)
			if (!strcmp(converter[i].configInput, enumValue))
				return new XMLConfigElement<OmmState::StreamState>(name, parent, ConfigElement::ConfigElementTypeEnum, converter[i].convertedValue);
	}
	else if (!strcmp(enumType, "DataState"))
	{
		static struct
		{
			const char* configInput;
			OmmState::DataState convertedValue;
		} converter[] =
		{
			{ "NoChange", OmmState::NoChangeEnum },
			{ "Ok", OmmState::OkEnum },
			{ "Suspect", OmmState::SuspectEnum },
		};

		for (int i = 0; i < sizeof converter / sizeof converter[0]; i++)
			if (!strcmp(converter[i].configInput, enumValue))
				return new XMLConfigElement<OmmState::DataState>(name, parent, ConfigElement::ConfigElementTypeEnum, converter[i].convertedValue);
	}
	else if (!strcmp(enumType, "StatusCode"))
	{
		static struct
		{
			const char* configInput;
			OmmState::StatusCode convertedValue;
		} converter[] =
		{
			{ "None", OmmState::NoneEnum },
			{ "NotFound", OmmState::NotFoundEnum },
			{ "Timeout", OmmState::TimeoutEnum },
			{ "NotAuthorized", OmmState::NotAuthorizedEnum },
			{ "InvalidArgument", OmmState::InvalidArgumentEnum },
			{ "UsageError", OmmState::UsageErrorEnum },
			{ "Preempted", OmmState::PreemptedEnum },
			{ "JustInTimeConflationStarted", OmmState::JustInTimeConflationStartedEnum },
			{ "TickByTickResumed", OmmState::TickByTickResumedEnum },
			{ "FailoverStarted", OmmState::FailoverStartedEnum },
			{ "FailoverCompleted", OmmState::FailoverCompletedEnum },
			{ "GapDetected", OmmState::GapDetectedEnum },
			{ "NoResources", OmmState::NoResourcesEnum },
			{ "TooManyItems", OmmState::TooManyItemsEnum },
			{ "AlreadyOpen", OmmState::AlreadyOpenEnum },
			{ "SourceUnknown", OmmState::SourceUnknownEnum },
			{ "NotOpen", OmmState::NotOpenEnum },
			{ "NonUpdatingItem", OmmState::NonUpdatingItemEnum },
			{ "UnsupportedViewType", OmmState::UnsupportedViewTypeEnum },
			{ "InvalidView", OmmState::InvalidViewEnum },
			{ "FullViewProvided", OmmState::FullViewProvidedEnum },
			{ "UnableToRequestAsBatch", OmmState::UnableToRequestAsBatchEnum },
			{ "NoBatchViewSupportInReq", OmmState::NoBatchViewSupportInReqEnum },
			{ "ExceededMaxMountsPerUser", OmmState::ExceededMaxMountsPerUserEnum },
			{ "Error", OmmState::ErrorEnum },
			{ "DacsDown", OmmState::DacsDownEnum },
			{ "UserUnknownToPermSys", OmmState::UserUnknownToPermSysEnum },
			{ "DacsMaxLoginsReached", OmmState::DacsMaxLoginsReachedEnum },
			{ "DacsUserAccessToAppDenied", OmmState::DacsUserAccessToAppDeniedEnum },
		};

		for (int i = 0; i < sizeof converter / sizeof converter[0]; i++)
			if (!strcmp(converter[i].configInput, enumValue))
				return new XMLConfigElement<OmmState::StatusCode>(name, parent, ConfigElement::ConfigElementTypeEnum, converter[i].convertedValue);
	}
	else
	{
		errorMsg.append("no implementation in convertEnum for enumType [").append(enumType.c_str()).append("]");
		return 0;
	}

	errorMsg.append("convertEnum has an implementation for enumType [").append(enumType.c_str()).append("] but no appropriate conversion for value [").append(enumValue.c_str()).append("]");
	return 0;
}

void EmaConfigBaseImpl::config(const Data& config)
{
	if (config.getDataType() == DataType::MapEnum)
	{
		if (!_pProgrammaticConfigure)
		{
			_pProgrammaticConfigure = new ProgrammaticConfigure(static_cast<const Map&>(config), _pEmaConfig->errors());
		}
		else
		{
			_pProgrammaticConfigure->addConfigure(static_cast<const Map&>(config));
		}
	}
	else
	{
		EmaString temp("Invalid Data type='");
		temp.append(getDTypeAsString(config.getDataType())).append("' for Programmatic Configure.");
		EmaConfigError* mce(new EmaConfigError(temp, OmmLoggerClient::ErrorEnum));
		_pEmaConfig->errors().add(mce);
	}
}

void EmaConfigBaseImpl::getLoggerName(const EmaString& instanceName, EmaString& retVal) const
{
	if (_pProgrammaticConfigure && _pProgrammaticConfigure->getActiveLoggerName(instanceName, retVal))
		return;

	EmaString nodeName(_instanceNodeName);
	nodeName.append(instanceName).append("|Logger");

	get<EmaString>(nodeName, retVal);
}

void EmaConfigBaseImpl::getAsciiAttributeValueList(const EmaString& nodeName, const EmaString& attributeName, EmaVector< EmaString >& entryValues)
{
	_pEmaConfig->getAsciiAttributeValueList(nodeName, attributeName, entryValues);
}

void EmaConfigBaseImpl::getEntryNodeList(const EmaString& nodeName, const EmaString& entryName, EmaVector< XMLnode* >& entryNodeList)
{
	_pEmaConfig->getEntryNodeList(nodeName, entryName, entryNodeList);
}

void EmaConfigBaseImpl::getServiceNames(const EmaString& directoryName, EmaVector< EmaString >& serviceNames)
{
	_pEmaConfig->getServiceNameList(directoryName, serviceNames);
}


EmaConfigImpl::EmaConfigImpl() :
	_loginRdmReqMsg( *this ),
	_pDirectoryRsslRequestMsg( 0 ),
	_pRdmFldRsslRequestMsg( 0 ),
	_pEnumDefRsslRequestMsg( 0 ),
	_pDirectoryRsslRefreshMsg( 0 ),
	_hostnameSetViaFunctionCall(),
	_portSetViaFunctionCall()
{
}

EmaConfigImpl::~EmaConfigImpl()
{
	if ( _pDirectoryRsslRefreshMsg )
		delete _pDirectoryRsslRefreshMsg;

	if ( _pDirectoryRsslRequestMsg )
		delete _pDirectoryRsslRequestMsg;

	if ( _pRdmFldRsslRequestMsg )
		delete _pRdmFldRsslRequestMsg;

	if ( _pEnumDefRsslRequestMsg )
		delete _pEnumDefRsslRequestMsg;

	if ( _pProgrammaticConfigure )
		delete _pProgrammaticConfigure;
}

void EmaConfigImpl::clear()
{
	_loginRdmReqMsg.clear();

	if ( _pDirectoryRsslRefreshMsg )
		_pDirectoryRsslRefreshMsg->clear();

	if ( _pDirectoryRsslRequestMsg )
		_pDirectoryRsslRequestMsg->clear();

	if ( _pRdmFldRsslRequestMsg )
		_pRdmFldRsslRequestMsg->clear();

	if ( _pEnumDefRsslRequestMsg )
		_pEnumDefRsslRequestMsg->clear();

	if ( _pProgrammaticConfigure )
		_pProgrammaticConfigure->clear();

	_instanceNodeName.clear();
}

void EmaConfigImpl::username( const EmaString& username )
{
	_loginRdmReqMsg.username( username );
}

void EmaConfigImpl::password( const EmaString& password )
{
	_loginRdmReqMsg.password( password );
}

void EmaConfigImpl::position( const EmaString& position )
{
	_loginRdmReqMsg.position( position );
}

void EmaConfigImpl::applicationId( const EmaString& applicationId )
{
	_loginRdmReqMsg.applicationId( applicationId );
}

void EmaConfigImpl::applicationName( const EmaString& applicationName )
{
	_loginRdmReqMsg.applicationName( applicationName );
}

void EmaConfigImpl::instanceId( const EmaString& instanceId )
{
	_loginRdmReqMsg.instanceId( instanceId );
}

void EmaConfigImpl::host( const EmaString& host )
{
	_portSetViaFunctionCall.userSet = true;
	Int32 index = host.find( ":", 0 );
	if ( index == -1 )
	{
		if ( host.length() )
			_hostnameSetViaFunctionCall = host;
		else
			_hostnameSetViaFunctionCall = DEFAULT_HOST_NAME;
	}

	else if ( index == 0 )
	{
		_hostnameSetViaFunctionCall = DEFAULT_HOST_NAME;

		if ( host.length() > 1 )
			_portSetViaFunctionCall.userSpecifiedValue = host.substr( 1, host.length() - 1 );
	}

	else
	{
		_hostnameSetViaFunctionCall = host.substr( 0, index );
		if ( host.length() > static_cast<UInt32>( index + 1 ) )
			_portSetViaFunctionCall.userSpecifiedValue = host.substr( index + 1, host.length() - index - 1 );
	}
}

void EmaConfigImpl::addAdminMsg( const ReqMsg& reqMsg )
{
	RsslRequestMsg* pRsslRequestMsg = static_cast<const ReqMsgEncoder&>( reqMsg.getEncoder() ).getRsslRequestMsg();

	switch ( pRsslRequestMsg->msgBase.domainType )
	{
	case RSSL_DMT_LOGIN:
		addLoginReqMsg( pRsslRequestMsg );
		break;
	case RSSL_DMT_DICTIONARY:
		addDictionaryReqMsg( pRsslRequestMsg, static_cast<const ReqMsgEncoder&>( reqMsg.getEncoder() ).hasServiceName() ?
			&static_cast<const ReqMsgEncoder&>( reqMsg.getEncoder() ).getServiceName() : 0 );
		break;
	case RSSL_DMT_SOURCE:
		addDirectoryReqMsg( pRsslRequestMsg );
		break;
	default:
	{
		EmaString temp( "Request message with unhandled domain passed into addAdminMsg( const ReqMsg& ). Domain type='" );
		temp.append( pRsslRequestMsg->msgBase.domainType ).append( "'. " );
		EmaConfigError* mce( new EmaConfigError( temp, OmmLoggerClient::ErrorEnum ) );
		_pEmaConfig->errors().add( mce );
	}
	break;
	}
}

void EmaConfigImpl::addAdminMsg( const RefreshMsg& refreshMsg )
{
	RsslRefreshMsg* pRsslRefreshMsg = static_cast<const RefreshMsgEncoder&>( refreshMsg.getEncoder() ).getRsslRefreshMsg();

	switch ( pRsslRefreshMsg->msgBase.domainType )
	{
	case RSSL_DMT_LOGIN:
		break;
	case RSSL_DMT_DICTIONARY:
		break;
	case RSSL_DMT_SOURCE:
	{
		if ( pRsslRefreshMsg->msgBase.streamId > 0 )
		{
			EmaString temp( "Refresh passed into addAdminMsg( const RefreshMsg& ) contains unhandled stream id. StreamId='" );
			temp.append( pRsslRefreshMsg->msgBase.streamId ).append( "'. " );
			EmaConfigError* mce( new EmaConfigError( temp, OmmLoggerClient::ErrorEnum ) );
			_pEmaConfig->errors().add( mce );
			return;
		}

		if ( pRsslRefreshMsg->msgBase.containerType != RSSL_DT_MAP )
		{
			EmaString temp( "RefreshMsg with SourceDirectory passed into addAdminMsg( const RefreshMsg& ) contains a container with wrong data type. Expected container data type is Map. Passed in is " );
			temp += DataType( dataType[pRsslRefreshMsg->msgBase.containerType] ).toString();
			EmaConfigError* mce( new EmaConfigError( temp, OmmLoggerClient::ErrorEnum ) );
			_pEmaConfig->errors().add( mce );
			return;
		}

		addDirectoryRefreshMsg( pRsslRefreshMsg );
	}
	break;
	default:
	{
		EmaString temp( "Refresh message passed into addAdminMsg( const RefreshMsg& ) contains unhandled domain type. Domain type='" );
		temp.append( pRsslRefreshMsg->msgBase.domainType ).append( "'. " );
		EmaConfigError* mce( new EmaConfigError( temp, OmmLoggerClient::ErrorEnum ) );
		_pEmaConfig->errors().add( mce );
	}
	break;
	}
}

void EmaConfigImpl::getChannelName( const EmaString& instanceName, EmaString& retVal ) const
{
	if ( _pProgrammaticConfigure && _pProgrammaticConfigure->getActiveChannelName( instanceName, retVal ) )
		return;

	EmaString nodeName( _instanceNodeName );
	nodeName.append( instanceName );
	nodeName.append( "|Channel" );

	get<EmaString>( nodeName, retVal );
}

void EmaConfigImpl::addLoginReqMsg( RsslRequestMsg* pRsslRequestMsg )
{
	_loginRdmReqMsg.set( pRsslRequestMsg );
}

void EmaConfigImpl::addDictionaryReqMsg( RsslRequestMsg* pRsslRequestMsg, const EmaString* serviceName )
{
	if ( !( pRsslRequestMsg->msgBase.msgKey.flags & RSSL_MKF_HAS_NAME ) )
	{
		EmaString temp( "Received dicionary request message contains no dictionary name. Message ignored." );
		_pEmaConfig->appendErrorMessage( temp, OmmLoggerClient::ErrorEnum );
		return;
	}
	if ( !( pRsslRequestMsg->msgBase.msgKey.flags & RSSL_MKF_HAS_SERVICE_ID ) )
	{
		if ( !serviceName )
		{
			EmaString temp( "Received dicionary request message contains no serviceId or service name. Message ignored." );
			_pEmaConfig->appendErrorMessage( temp, OmmLoggerClient::ErrorEnum );
			return;
		}
	}
	else if ( !( pRsslRequestMsg->msgBase.msgKey.flags & RSSL_MKF_HAS_FILTER ) )
	{
		EmaString temp( "Received dicionary request message contains no filter. Message ignored." );
		_pEmaConfig->appendErrorMessage( temp, OmmLoggerClient::ErrorEnum );
		return;
	}
	else if ( pRsslRequestMsg->flags & RSSL_RQMF_NO_REFRESH )
	{
		EmaString temp( "Received dicionary request message contains no_refresh flag. Message ignored." );
		_pEmaConfig->appendErrorMessage( temp, OmmLoggerClient::ErrorEnum );
		return;
	}

	RsslBuffer rdmFldDictionaryName;
	rdmFldDictionaryName.data = ( char* ) "RWFFld";
	rdmFldDictionaryName.length = 6;

	RsslBuffer enumtypedefName;
	enumtypedefName.data = ( char* ) "RWFEnum";
	enumtypedefName.length = 7;

	if ( rsslBufferIsEqual( &pRsslRequestMsg->msgBase.msgKey.name, &rdmFldDictionaryName ) )
	{
		if ( !_pRdmFldRsslRequestMsg )
			_pRdmFldRsslRequestMsg = new AdminReqMsg( *this );

		_pRdmFldRsslRequestMsg->set( pRsslRequestMsg );

		if ( serviceName )
			_pRdmFldRsslRequestMsg->setServiceName( *serviceName );
	}
	else if ( rsslBufferIsEqual( &pRsslRequestMsg->msgBase.msgKey.name, &enumtypedefName ) )
	{
		if ( !_pEnumDefRsslRequestMsg )
			_pEnumDefRsslRequestMsg = new AdminReqMsg( *this );

		_pEnumDefRsslRequestMsg->set( pRsslRequestMsg );

		if ( serviceName )
			_pEnumDefRsslRequestMsg->setServiceName( *serviceName );
	}
	else
	{
		EmaString temp( "Received dicionary request message contains unrecognized dictionary name. Message ignored." );
		_pEmaConfig->appendErrorMessage( temp, OmmLoggerClient::ErrorEnum );
	}
}

void EmaConfigImpl::addDirectoryReqMsg( RsslRequestMsg* pRsslRequestMsg )
{
	if ( !_pDirectoryRsslRequestMsg )
		_pDirectoryRsslRequestMsg = new AdminReqMsg( *this );

	_pDirectoryRsslRequestMsg->set( pRsslRequestMsg );
}

void EmaConfigImpl::addDirectoryRefreshMsg( RsslRefreshMsg* pRsslRefreshMsg )
{
	if ( !_pDirectoryRsslRefreshMsg )
		_pDirectoryRsslRefreshMsg = new AdminRefreshMsg( this );

	_pDirectoryRsslRefreshMsg->set( pRsslRefreshMsg );
}

RsslRDMLoginRequest* EmaConfigImpl::getLoginReq()
{
	return _loginRdmReqMsg.get();
}

RsslRequestMsg* EmaConfigImpl::getDirectoryReq()
{
	return _pDirectoryRsslRequestMsg ? _pDirectoryRsslRequestMsg->get() : 0;
}

AdminReqMsg* EmaConfigImpl::getRdmFldDictionaryReq()
{
	return _pRdmFldRsslRequestMsg ? _pRdmFldRsslRequestMsg : 0;
}

AdminReqMsg* EmaConfigImpl::getEnumDefDictionaryReq()
{
	return _pEnumDefRsslRequestMsg ? _pEnumDefRsslRequestMsg : 0;
}

AdminRefreshMsg* EmaConfigImpl::getDirectoryRefreshMsg()
{
	AdminRefreshMsg* pTemp = _pDirectoryRsslRefreshMsg;
	_pDirectoryRsslRefreshMsg = 0;
	return pTemp;
}

EmaConfigServerImpl::EmaConfigServerImpl() :
	_portSetViaFunctionCall(),
	_pDirectoryRsslRefreshMsg(0)
{
}

EmaConfigServerImpl::~EmaConfigServerImpl()
{
}

void EmaConfigServerImpl::clear()
{
	EmaConfigBaseImpl::clear();
}

void EmaConfigServerImpl::addAdminMsg( const RefreshMsg& refreshMsg )
{
	RsslRefreshMsg* pRsslRefreshMsg = static_cast<const RefreshMsgEncoder&>(refreshMsg.getEncoder()).getRsslRefreshMsg();

	switch ( pRsslRefreshMsg->msgBase.domainType )
	{
		case RSSL_DMT_LOGIN:
			break;
		case RSSL_DMT_DICTIONARY:
		{
			// Todo: add code to handle to store dictionary message
		}
		break;
		case RSSL_DMT_SOURCE:
		{
			if (pRsslRefreshMsg->msgBase.streamId > 0)
			{
				EmaString temp("Refresh passed into addAdminMsg( const RefreshMsg& ) contains unhandled stream id. StreamId='");
				temp.append(pRsslRefreshMsg->msgBase.streamId).append("'. ");
				EmaConfigError* mce(new EmaConfigError(temp, OmmLoggerClient::ErrorEnum));
				_pEmaConfig->errors().add(mce);
				return;
			}

			if (pRsslRefreshMsg->msgBase.containerType != RSSL_DT_MAP)
			{
				EmaString temp("RefreshMsg with SourceDirectory passed into addAdminMsg( const RefreshMsg& ) contains a container with wrong data type. Expected container data type is Map. Passed in is ");
				temp += DataType(dataType[pRsslRefreshMsg->msgBase.containerType]).toString();
				EmaConfigError* mce(new EmaConfigError(temp, OmmLoggerClient::ErrorEnum));
				_pEmaConfig->errors().add(mce);
				return;
			}

			addDirectoryRefreshMsg(pRsslRefreshMsg);
		}
		break;
		default:
		{
			EmaString temp("Refresh message passed into addAdminMsg( const RefreshMsg& ) contains unhandled domain type. Domain type='");
			temp.append(pRsslRefreshMsg->msgBase.domainType).append("'. ");
			EmaConfigError* mce(new EmaConfigError(temp, OmmLoggerClient::ErrorEnum));
			_pEmaConfig->errors().add(mce);
		}
		break;
	}
}

void EmaConfigServerImpl::port(const EmaString& port)
{
	_portSetViaFunctionCall.userSet = true;
	_portSetViaFunctionCall.userSpecifiedValue = port;
}

const PortSetViaFunctionCall& EmaConfigServerImpl::getUserSpecifiedPort() const
{
	return _portSetViaFunctionCall;
}

void EmaConfigServerImpl::getServerName( const EmaString& instanceName, EmaString& retVal) const
{
	// Todo: add implementation to query from programmatic configuration

	EmaString nodeName(_instanceNodeName);
	nodeName.append(instanceName);
	nodeName.append("|Server");

	get<EmaString>(nodeName, retVal);
}

bool EmaConfigServerImpl::getDictionaryName(const EmaString& instanceName, EmaString& retVal) const
{
	if (!_pProgrammaticConfigure || !_pProgrammaticConfigure->getActiveDictionaryName(instanceName, retVal))
	{
		EmaString nodeName(_instanceNodeName);
		nodeName.append(instanceName);
		nodeName.append("|Dictionary");
		get<EmaString>(nodeName, retVal);
	}

	return true;
}

void EmaConfigServerImpl::addDirectoryRefreshMsg(RsslRefreshMsg* pRsslRefreshMsg)
{
	if (!_pDirectoryRsslRefreshMsg)
		_pDirectoryRsslRefreshMsg = new AdminRefreshMsg(this);

	_pDirectoryRsslRefreshMsg->set(pRsslRefreshMsg);
}

AdminRefreshMsg* EmaConfigServerImpl::getDirectoryRefreshMsg()
{
	AdminRefreshMsg* pTemp = _pDirectoryRsslRefreshMsg;
	_pDirectoryRsslRefreshMsg = 0;
	return pTemp;
}

void XMLnode::print( int tabs )
{
	printf( "%s (level %d, this %p, parent %p)\n", _name.c_str(), _level, this, _parent );
	fflush( stdout );
	++tabs;
	_attributes->print( tabs );
	_children->print( tabs );
	--tabs;
}

void XMLnode::appendErrorMessage( const EmaString& errorMsg, OmmLoggerClient::Severity severity )
{
	if ( _parent )
		_parent->appendErrorMessage( errorMsg, severity );
	else
	{
		EmaConfigError* mce( new EmaConfigError( errorMsg, severity ) );
		_errors.add( mce );
	}
}

void XMLnode::verifyDefaultConsumer()
{
	const EmaString* defaultName( find< EmaString >( EmaString( "ConsumerGroup|DefaultConsumer" ) ) );
	if ( defaultName )
	{
		XMLnode* consumerList( find< XMLnode >( "ConsumerGroup|ConsumerList" ) );
		if ( consumerList )
		{
			EmaList< NameString* > theNames;
			consumerList->getNames( theNames );

			if ( theNames.empty() && *defaultName == "EmaConsumer" )
				return;

			NameString* name( theNames.pop_front() );
			while ( name )
			{
				if ( *name == *defaultName )
					return;
				else name = theNames.pop_front();
			}
			EmaString errorMsg( "specified default consumer name [" );
			errorMsg.append( *defaultName ).append( "] was not found in the configured consumers" );
			throwIceException( errorMsg );
		}
		else if ( *defaultName != "EmaConsumer" )
		{
			EmaString errorMsg( "default consumer name [" );
			errorMsg.append( *defaultName ).append( "] was specified, but no consumers with this name were configured" );
			throwIceException( errorMsg );
		}
	}
}

void XMLnode::verifyDefaultNiProvider()
{
	const EmaString* defaultName( find< EmaString >( EmaString( "NiProviderGroup|DefaultNiProvider" ) ) );
	if ( defaultName )
	{
		XMLnode* niProviderList( find< XMLnode >( "NiProviderGroup|NiProviderList" ) );
		if ( niProviderList )
		{
			EmaList< NameString* > theNames;
			niProviderList->getNames( theNames );

			if ( theNames.empty() && *defaultName == "EmaNiProvider" )
				return;

			NameString* name( theNames.pop_front() );
			while ( name )
			{
				if ( *name == *defaultName )
					return;
				else name = theNames.pop_front();
			}
			EmaString errorMsg( "specified default ni provider name [" );
			errorMsg.append( *defaultName ).append( "] was not found in the configured ni providers" );
			throwIceException( errorMsg );
		}
		else if ( *defaultName != "EmaNiProvider" )
		{
			EmaString errorMsg( "default ni provider name [" );
			errorMsg.append( *defaultName ).append( "] was specified, but no ni providers with this name were configured" );
			throwIceException( errorMsg );
		}
	}
}

void XMLnode::verifyDefaultDirectory()
{
	const EmaString* defaultName( find< EmaString >( EmaString( "DirectoryGroup|DefaultDirectory" ) ) );
	if ( defaultName )
	{
		XMLnode* niProviderList( find< XMLnode >( "DirectoryGroup|DirectoryList" ) );
		if ( niProviderList )
		{
			EmaList< NameString* > theNames;
			niProviderList->getNames( theNames );

			if ( theNames.empty() && *defaultName == "EmaDirectory" )
				return;

			NameString* name( theNames.pop_front() );
			while ( name )
			{
				if ( *name == *defaultName )
					return;
				else name = theNames.pop_front();
			}
			EmaString errorMsg( "specified default directory name [" );
			errorMsg.append( *defaultName ).append( "] was not found in the configured directories" );
			throwIceException( errorMsg );
		}
		else if ( *defaultName != "EmaDirectory" )
		{
			EmaString errorMsg( "default ni directory name [" );
			errorMsg.append( *defaultName ).append( "] was specified, but no ni directories with this name were configured" );
			throwIceException( errorMsg );
		}
	}
}

void XMLnode::getServiceNameList( const EmaString& directoryName, EmaVector< EmaString >& serviceNames )
{
	serviceNames.clear();
	XMLnode* serviceList( find< XMLnode >( EmaString( "DirectoryGroup|DirectoryList|Directory." ) + directoryName ) );
	if ( serviceList )
	{
		EmaList< NameString* > theNames;
		serviceList->getNames( theNames );

		NameString* pTempName = theNames.pop_front();

		while ( pTempName )
		{
			serviceNames.push_back( *pTempName );
			delete pTempName;
			pTempName = theNames.pop_front();
		}
	}
}

void XMLnode::getAsciiAttributeValueList( const EmaString& nodeName, const EmaString& attributeName, EmaVector< EmaString >& valueList )
{
	valueList.clear();

	XMLnode* pNode = find< XMLnode >( nodeName );
	if ( pNode )
		pNode->getValues( attributeName, valueList );
}

void XMLnode::getEntryNodeList( const EmaString& nodeName, const EmaString& entryName, EmaVector< XMLnode* >& entryNodeList )
{
	entryNodeList.clear();

	XMLnode* pNode = find< XMLnode >( nodeName );

	if ( !pNode ) return;

	xmlList< XMLnode >* pChildren = pNode->getChildren();

	if ( !pChildren ) return;

	XMLnode* pChild = pChildren->getFirst();

	while ( pChild )
	{
		if ( entryName.empty() || pChild->name() == entryName )
			entryNodeList.push_back( pChild );

		pChild = pChildren->getNext( pChild );
	}
}

bool XMLnode::addChild( XMLnode* child )
{
	for ( int i = 0; i < sizeof NodesThatRequireName / sizeof NodesThatRequireName[0]; ++i )
		if ( child->_name == NodesThatRequireName[i] )
		{

			const ConfigElement* tmp( child->_attributes->first() );
			while ( tmp )
			{
				if ( tmp->name() == "Name" )
					return _children->insert( child, true );
				tmp = child->_attributes->next( tmp );
			}

			EmaString errorMsg( "cannot add node with name [" );
			errorMsg += child->_name
				+ "] because node is missing \"name\" attribute; node will be ignored";
			appendErrorMessage( errorMsg, OmmLoggerClient::WarningEnum );
			return false;
		}

	return _children->insert( child, false );
}

EmaConfigError::EmaConfigError( const EmaString& errorMsg, OmmLoggerClient::Severity severity ) :
	_errorMsg( errorMsg ),
	_severity( severity )
{
}

EmaConfigError::~EmaConfigError()
{
}

OmmLoggerClient::Severity EmaConfigError::severity() const
{
	return _severity;
}

const EmaString& EmaConfigError::errorMsg() const
{
	return _errorMsg;
}

void EmaConfigErrorList::add( EmaConfigError* mce )
{
	ListElement* le( new ListElement( mce ) );
	if ( _pList )
	{
		ListElement* p;
		for ( p = _pList; p; p = p->next )
			if ( !p->next )
				break;
		p->next = le;
	}
	else
		_pList = le;
	++_count;
}

EmaConfigErrorList::EmaConfigErrorList() :
	_pList( 0 ),
	_count( 0 )
{
}

EmaConfigErrorList::~EmaConfigErrorList()
{
	clear();
}

UInt32 EmaConfigErrorList::count() const
{
	return _count;
}

void EmaConfigErrorList::add( EmaConfigErrorList& eL )
{
	if ( eL._count )
	{
		if ( _pList )
		{
			ListElement* p = _pList;
			while ( p->next )
				p = p->next;
			p->next = eL._pList;
		}
		else
			_pList = eL._pList;
		_count += eL.count();
	}
};

void EmaConfigErrorList::clear()
{
	if ( _pList )
	{
		ListElement* q;
		for ( ListElement* p = _pList; p; p = q )
		{
			q = p->next;
			delete( p->error );
			delete( p );
		}
		_pList = 0;

	}
	_count = 0;
}

void EmaConfigErrorList::printErrors( OmmLoggerClient::Severity severity )
{
	bool printed( false );
	if ( _pList )
	{
		for ( ListElement* p = _pList; p; p = p = p->next )
			if ( p->error->severity() >= severity )
			{
				if ( !printed )
				{
					printf( "begin configuration errors:\n" );
					printed = true;
				}
				printf( "\t[%s] %s\n", OmmLoggerClient::loggerSeverityString( p->error->severity() ), p->error->errorMsg().c_str() );
			}
		if ( printed )
			printf( "end configuration errors\n" );
		else
			printf( "no configuration errors existed with level equal to or exceeding %s\n", OmmLoggerClient::loggerSeverityString( severity ) );
	}
	else
		printf( "no configuration errors found\n" );

}

void EmaConfigErrorList::log( OmmLoggerClient* logger, OmmLoggerClient::Severity severity )
{
	for ( ListElement* p = _pList; p; p = p->next )
		if ( p->error->severity() >= severity )
			logger->log( "EmaConfig", p->error->severity(), p->error->errorMsg().c_str() );
}


namespace thomsonreuters {

namespace ema {

namespace access {

template<>
void XMLConfigElement< EmaString >::print() const
{
	printf( "%s (parent %p)", _name.c_str(), _parent );
	printf( ": \"%s\"", _values[0].c_str() );
	for ( unsigned int i = 1; i < _values.size(); ++i )
		printf( ", \"%s\"", _values[i].c_str() );
}

template<>
void XMLConfigElement< bool >::print() const
{
	printf( "%s (parent %p)", _name.c_str(), _parent );
	printf( _values[0] == true ? ": true" : ": false" );
	for ( unsigned int i = 0; i < _values.size(); ++i )
		printf( _values[i] == true ? ", true" : ", false" );
}

template<>
bool XMLConfigElement<Int64>::operator== ( const XMLConfigElement<Int64>& rhs ) const
{
	for ( unsigned int i = 0; i < _values.size(); ++i )
		if ( _values[0] != rhs._values[0] )
			return false;
	return true;
}

template<>
bool XMLConfigElement<EmaString>::operator== ( const XMLConfigElement<EmaString>& rhs ) const
{
	for ( unsigned int i = 0; i < _values.size(); ++i )
		if ( _values[0] != rhs._values[0] )
			return false;
	return true;
}

}

}

}

bool ConfigElement::operator== ( const ConfigElement& rhs ) const
{
	if ( _name == rhs._name && type() == rhs.type() )
		switch ( type() )
		{
		case ConfigElementTypeInt64:
		{
			const XMLConfigElement<Int64>& l = dynamic_cast<const XMLConfigElement<Int64> &>( *this );
			const XMLConfigElement<Int64>& r = dynamic_cast<const XMLConfigElement<Int64> &>( rhs );
			return l == r ? true : false;
		}
		case ConfigElementTypeAscii:
		{
			const XMLConfigElement<EmaString>& l = dynamic_cast<const XMLConfigElement<EmaString> &>( *this );
			const XMLConfigElement<EmaString>& r = dynamic_cast<const XMLConfigElement<EmaString> &>( rhs );
			return l == r ? true : false;
		}
		case ConfigElementTypeEnum:
		{
			break;
		}
		}

	return false;
}

void ConfigElement::appendErrorMessage( EmaString& errorMsg, OmmLoggerClient::Severity severity )
{
	_parent->appendErrorMessage( errorMsg, severity );
}

namespace thomsonreuters
{

namespace ema
{

namespace access
{

template< typename T >
EmaString
XMLConfigElement< T >::changeMessage( const EmaString& actualName, const XMLConfigElement< T >& newElement ) const
{
	EmaString msg( "value for element [" );
	if ( actualName.empty() )
		msg.append( newElement.name() );
	else
		msg.append( actualName ).append( "|" ).append( newElement.name() );
	msg.append( "] changing from [" ).append( *( const_cast<XMLConfigElement<T> *>( this )->value() ) ).append( "] to [" )
		.append( *( const_cast<XMLConfigElement<T> &>( newElement ).value() ) ).append( "]" );
	return msg;
}

}

}

}

EmaString ConfigElement::changeMessage( const EmaString& actualName, const ConfigElement& newElement ) const
{
	switch ( type() )
	{
	case ConfigElementTypeAscii:
	{
		const XMLConfigElement<EmaString>& l = dynamic_cast<const XMLConfigElement<EmaString> &>( *this );
		const XMLConfigElement<EmaString>& r = dynamic_cast<const XMLConfigElement<EmaString> &>( newElement );
		EmaString retVal( l.changeMessage( actualName, r ) );
		return retVal;
	}
	case ConfigElementTypeInt64:
	{
		const XMLConfigElement< Int64 >& l = dynamic_cast<const XMLConfigElement< Int64 > &>( *this );
		const XMLConfigElement< Int64 >& r = dynamic_cast<const XMLConfigElement< Int64 > &>( newElement );
		EmaString retVal( l.changeMessage( actualName, r ) );
		return retVal;
	}
	case ConfigElementTypeUInt64:
	{
		const XMLConfigElement< UInt64 >& l = dynamic_cast<const XMLConfigElement< UInt64 > &>( *this );
		const XMLConfigElement< UInt64 >& r = dynamic_cast<const XMLConfigElement< UInt64 > &>( newElement );
		EmaString retVal( l.changeMessage( actualName, r ) );
		return retVal;
	}
	default:
	{
		EmaString defaultMsg( "element [" );
		defaultMsg.append( newElement.name() ).append( "] change; no information on exact change in values" );
		return defaultMsg;
	}
	}
}

AdminReqMsg::AdminReqMsg( EmaConfigImpl& configImpl ) :
	_emaConfigImpl( configImpl ),
	_hasServiceName( false ),
	_serviceName()
{
	rsslClearRequestMsg( &_rsslMsg );
	rsslClearBuffer( &_name );
	rsslClearBuffer( &_header );
	rsslClearBuffer( &_attrib );
	rsslClearBuffer( &_payload );
}

AdminReqMsg::~AdminReqMsg()
{
	if ( _payload.data )
		free( _payload.data );

	if ( _attrib.data )
		free( _attrib.data );

	if ( _header.data )
		free( _header.data );

	if ( _name.data )
		free( _name.data );
}

AdminReqMsg& AdminReqMsg::set( RsslRequestMsg* pRsslRequestMsg )
{
	_rsslMsg = *pRsslRequestMsg;

	if ( _rsslMsg.flags & RSSL_RQMF_HAS_EXTENDED_HEADER )
	{
		if ( _rsslMsg.extendedHeader.length > _header.length )
		{
			if ( _header.data ) free( _header.data );

			_header.data = (char*) malloc( _rsslMsg.extendedHeader.length );
			_header.length = _rsslMsg.extendedHeader.length;
		}

		memcpy( _header.data, _rsslMsg.extendedHeader.data, _header.length );

		_rsslMsg.extendedHeader = _header;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.extendedHeader );
	}

	if ( _rsslMsg.msgBase.containerType != RSSL_DT_NO_DATA )
	{
		if ( _rsslMsg.msgBase.encDataBody.length > _payload.length )
		{
			if ( _payload.data ) free( _payload.data );

			_payload.data = (char*) malloc( _rsslMsg.msgBase.encDataBody.length );
			_payload.length = _rsslMsg.msgBase.encDataBody.length;
		}

		memcpy( _payload.data, _rsslMsg.msgBase.encDataBody.data, _payload.length );

		_rsslMsg.msgBase.encDataBody = _payload;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.msgBase.encDataBody );
	}

	if ( _rsslMsg.msgBase.msgKey.flags & RSSL_MKF_HAS_ATTRIB )
	{
		if ( _rsslMsg.msgBase.msgKey.encAttrib.length > _attrib.length )
		{
			if ( _attrib.data ) free( _attrib.data );

			_attrib.data = (char*) malloc( _rsslMsg.msgBase.msgKey.encAttrib.length );
			_attrib.length = _rsslMsg.msgBase.msgKey.encAttrib.length;
		}

		memcpy( _attrib.data, _rsslMsg.msgBase.msgKey.encAttrib.data, _attrib.length );

		_rsslMsg.msgBase.msgKey.encAttrib = _attrib;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.msgBase.msgKey.encAttrib );
	}

	if ( _rsslMsg.msgBase.msgKey.flags & RSSL_MKF_HAS_NAME )
	{
		if ( _rsslMsg.msgBase.msgKey.name.length > _name.length )
		{
			if ( _name.data ) free( _name.data );

			_name.data = (char*) malloc( _rsslMsg.msgBase.msgKey.name.length );
			_name.length = _rsslMsg.msgBase.msgKey.name.length;
		}

		memcpy( _name.data, _rsslMsg.msgBase.msgKey.name.data, _name.length );

		_rsslMsg.msgBase.msgKey.name = _name;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.msgBase.msgKey.name );
	}

	return *this;
}

AdminReqMsg& AdminReqMsg::clear()
{
	rsslClearRequestMsg( &_rsslMsg );
	_hasServiceName = false;

	return *this;
}

RsslRequestMsg* AdminReqMsg::get()
{
	return &_rsslMsg;
}

bool AdminReqMsg::hasServiceName()
{
	return _hasServiceName;
}

void AdminReqMsg::setServiceName( const EmaString& serviceName )
{
	_serviceName = serviceName;
	_hasServiceName = true;
}

const EmaString& AdminReqMsg::getServiceName()
{
	return _serviceName;
}

AdminRefreshMsg::AdminRefreshMsg( EmaConfigBaseImpl* pConfigImpl ) :
	_pEmaConfigImpl( pConfigImpl )
{
	rsslClearRefreshMsg( &_rsslMsg );
	rsslClearBuffer( &_name );
	rsslClearBuffer( &_header );
	rsslClearBuffer( &_attrib );
	rsslClearBuffer( &_payload );
	rsslClearBuffer( &_statusText );
}

AdminRefreshMsg::AdminRefreshMsg( const AdminRefreshMsg& other ) :
	_pEmaConfigImpl( 0 )
{
	rsslClearRefreshMsg( &_rsslMsg );
	rsslClearBuffer( &_name );
	rsslClearBuffer( &_header );
	rsslClearBuffer( &_attrib );
	rsslClearBuffer( &_payload );
	rsslClearBuffer( &_statusText );

	_rsslMsg = other._rsslMsg;

	if ( _rsslMsg.flags & RSSL_RQMF_HAS_EXTENDED_HEADER )
	{
		if ( _header.data ) free( _header.data );

		_header.data = (char*) malloc( _rsslMsg.extendedHeader.length );
		_header.length = _rsslMsg.extendedHeader.length;

		memcpy( _header.data, _rsslMsg.extendedHeader.data, _header.length );

		_rsslMsg.extendedHeader = _header;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.extendedHeader );
	}

	if ( _rsslMsg.msgBase.containerType != RSSL_DT_NO_DATA )
	{
		if ( _payload.data ) free( _payload.data );

		_payload.data = (char*) malloc( _rsslMsg.msgBase.encDataBody.length );
		_payload.length = _rsslMsg.msgBase.encDataBody.length;

		memcpy( _payload.data, _rsslMsg.msgBase.encDataBody.data, _payload.length );

		_rsslMsg.msgBase.encDataBody = _payload;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.msgBase.encDataBody );
	}

	if ( _rsslMsg.msgBase.msgKey.flags & RSSL_MKF_HAS_ATTRIB )
	{
		if ( _attrib.data ) free( _attrib.data );

		_attrib.data = (char*) malloc( _rsslMsg.msgBase.msgKey.encAttrib.length );
		_attrib.length = _rsslMsg.msgBase.msgKey.encAttrib.length;

		memcpy( _attrib.data, _rsslMsg.msgBase.msgKey.encAttrib.data, _attrib.length );

		_rsslMsg.msgBase.msgKey.encAttrib = _attrib;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.msgBase.msgKey.encAttrib );
	}

	if ( _rsslMsg.msgBase.msgKey.flags & RSSL_MKF_HAS_NAME )
	{
		if ( _name.data ) free( _name.data );

		_name.data = (char*) malloc( _rsslMsg.msgBase.msgKey.name.length );
		_name.length = _rsslMsg.msgBase.msgKey.name.length;

		memcpy( _name.data, _rsslMsg.msgBase.msgKey.name.data, _name.length );

		_rsslMsg.msgBase.msgKey.name = _name;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.msgBase.msgKey.name );
	}

	if ( _rsslMsg.state.text.data )
	{
		if ( _statusText.data ) free( _statusText.data );

		_statusText.data = (char*) malloc( _rsslMsg.state.text.length );
		_statusText.length = _rsslMsg.state.text.length;

		memcpy( _statusText.data, _rsslMsg.state.text.data, _statusText.length );

		_rsslMsg.state.text = _statusText;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.state.text );
	}
}

AdminRefreshMsg& AdminRefreshMsg::operator=( const AdminRefreshMsg& other )
{
	_rsslMsg = other._rsslMsg;

	if ( _rsslMsg.flags & RSSL_RQMF_HAS_EXTENDED_HEADER )
	{
		if ( _rsslMsg.extendedHeader.length > _header.length )
		{
			if ( _header.data ) free( _header.data );

			_header.data = (char*) malloc( _rsslMsg.extendedHeader.length );
			_header.length = _rsslMsg.extendedHeader.length;
		}

		memcpy( _header.data, _rsslMsg.extendedHeader.data, _header.length );

		_rsslMsg.extendedHeader = _header;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.extendedHeader );
	}

	if ( _rsslMsg.msgBase.containerType != RSSL_DT_NO_DATA )
	{
		if ( _rsslMsg.msgBase.encDataBody.length > _payload.length )
		{
			if ( _payload.data ) free( _payload.data );

			_payload.data = (char*) malloc( _rsslMsg.msgBase.encDataBody.length );
			_payload.length = _rsslMsg.msgBase.encDataBody.length;
		}

		memcpy( _payload.data, _rsslMsg.msgBase.encDataBody.data, _payload.length );

		_rsslMsg.msgBase.encDataBody = _payload;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.msgBase.encDataBody );
	}

	if ( _rsslMsg.msgBase.msgKey.flags & RSSL_MKF_HAS_ATTRIB )
	{
		if ( _rsslMsg.msgBase.msgKey.encAttrib.length > _attrib.length )
		{
			if ( _attrib.data ) free( _attrib.data );

			_attrib.data = (char*) malloc( _rsslMsg.msgBase.msgKey.encAttrib.length );
			_attrib.length = _rsslMsg.msgBase.msgKey.encAttrib.length;
		}

		memcpy( _attrib.data, _rsslMsg.msgBase.msgKey.encAttrib.data, _attrib.length );

		_rsslMsg.msgBase.msgKey.encAttrib = _attrib;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.msgBase.msgKey.encAttrib );
	}

	if ( _rsslMsg.msgBase.msgKey.flags & RSSL_MKF_HAS_NAME )
	{
		if ( _rsslMsg.msgBase.msgKey.name.length > _name.length )
		{
			if ( _name.data ) free( _name.data );

			_name.data = (char*) malloc( _rsslMsg.msgBase.msgKey.name.length );
			_name.length = _rsslMsg.msgBase.msgKey.name.length;
		}

		memcpy( _name.data, _rsslMsg.msgBase.msgKey.name.data, _name.length );

		_rsslMsg.msgBase.msgKey.name = _name;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.msgBase.msgKey.name );
	}

	if ( _rsslMsg.state.text.data )
	{
		if ( _rsslMsg.state.text.length > _statusText.length )
		{
			if ( _statusText.data ) free( _statusText.data );

			_statusText.data = (char*) malloc( _rsslMsg.state.text.length );
			_statusText.length = _rsslMsg.state.text.length;
		}

		memcpy( _statusText.data, _rsslMsg.state.text.data, _statusText.length );

		_rsslMsg.state.text = _statusText;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.state.text );
	}

	return *this;
}

AdminRefreshMsg::~AdminRefreshMsg()
{
	if ( _statusText.data )
		free( _statusText.data );

	if ( _payload.data )
		free( _payload.data );

	if ( _attrib.data )
		free( _attrib.data );

	if ( _header.data )
		free( _header.data );

	if ( _name.data )
		free( _name.data );
}

AdminRefreshMsg& AdminRefreshMsg::set( RsslRefreshMsg* pRsslRefreshMsg )
{
	_rsslMsg = *pRsslRefreshMsg;

	if ( _rsslMsg.flags & RSSL_RQMF_HAS_EXTENDED_HEADER )
	{
		if ( _rsslMsg.extendedHeader.length > _header.length )
		{
			if ( _header.data ) free( _header.data );

			_header.data = (char*) malloc( _rsslMsg.extendedHeader.length );
			_header.length = _rsslMsg.extendedHeader.length;
		}

		memcpy( _header.data, _rsslMsg.extendedHeader.data, _header.length );

		_rsslMsg.extendedHeader = _header;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.extendedHeader );
	}

	if ( _rsslMsg.msgBase.containerType != RSSL_DT_NO_DATA )
	{
		if ( _rsslMsg.msgBase.encDataBody.length > _payload.length )
		{
			if ( _payload.data ) free( _payload.data );

			_payload.data = (char*) malloc( _rsslMsg.msgBase.encDataBody.length );
			_payload.length = _rsslMsg.msgBase.encDataBody.length;
		}

		memcpy( _payload.data, _rsslMsg.msgBase.encDataBody.data, _payload.length );

		_rsslMsg.msgBase.encDataBody = _payload;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.msgBase.encDataBody );
	}

	if ( _rsslMsg.msgBase.msgKey.flags & RSSL_MKF_HAS_ATTRIB )
	{
		if ( _rsslMsg.msgBase.msgKey.encAttrib.length > _attrib.length )
		{
			if ( _attrib.data ) free( _attrib.data );

			_attrib.data = (char*) malloc( _rsslMsg.msgBase.msgKey.encAttrib.length );
			_attrib.length = _rsslMsg.msgBase.msgKey.encAttrib.length;
		}

		memcpy( _attrib.data, _rsslMsg.msgBase.msgKey.encAttrib.data, _attrib.length );

		_rsslMsg.msgBase.msgKey.encAttrib = _attrib;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.msgBase.msgKey.encAttrib );
	}

	if ( _rsslMsg.msgBase.msgKey.flags & RSSL_MKF_HAS_NAME )
	{
		if ( _rsslMsg.msgBase.msgKey.name.length > _name.length )
		{
			if ( _name.data ) free( _name.data );

			_name.data = (char*) malloc( _rsslMsg.msgBase.msgKey.name.length );
			_name.length = _rsslMsg.msgBase.msgKey.name.length;
		}

		memcpy( _name.data, _rsslMsg.msgBase.msgKey.name.data, _name.length );

		_rsslMsg.msgBase.msgKey.name = _name;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.msgBase.msgKey.name );
	}

	if ( _rsslMsg.state.text.data )
	{
		if ( _rsslMsg.state.text.length > _statusText.length )
		{
			if ( _statusText.data ) free( _statusText.data );

			_statusText.data = (char*) malloc( _rsslMsg.state.text.length );
			_statusText.length = _rsslMsg.state.text.length;
		}

		memcpy( _statusText.data, _rsslMsg.state.text.data, _statusText.length );

		_rsslMsg.state.text = _statusText;
	}
	else
	{
		rsslClearBuffer( &_rsslMsg.state.text );
	}

	return *this;
}

AdminRefreshMsg& AdminRefreshMsg::clear()
{
	rsslClearRefreshMsg( &_rsslMsg );

	return *this;
}

RsslRefreshMsg* AdminRefreshMsg::get()
{
	return &_rsslMsg;
}

LoginRdmReqMsg::LoginRdmReqMsg( EmaConfigImpl& emaConfigImpl ) :
	_emaConfigImpl( emaConfigImpl ),
	_username(),
	_password(),
	_position(),
	_applicationId(),
	_applicationName(),
	_instanceId()
{
	rsslClearRDMLoginRequest( &_rsslRdmLoginRequest );
	_rsslRdmLoginRequest.rdmMsgBase.streamId = 1;
}

LoginRdmReqMsg::~LoginRdmReqMsg()
{
}

LoginRdmReqMsg& LoginRdmReqMsg::clear()
{
	_username.clear();
	_password.clear();
	_position.clear();
	_applicationId.clear();
	_applicationName.clear();
	_instanceId.clear();
	rsslClearRDMLoginRequest( &_rsslRdmLoginRequest );
	return *this;
}

LoginRdmReqMsg& LoginRdmReqMsg::set( RsslRequestMsg* pRsslRequestMsg )
{
	_rsslRdmLoginRequest.rdmMsgBase.domainType = RSSL_DMT_LOGIN;
	_rsslRdmLoginRequest.rdmMsgBase.rdmMsgType = RDM_LG_MT_REQUEST;
	_rsslRdmLoginRequest.flags = RDM_LG_RQF_NONE;

	if ( pRsslRequestMsg->msgBase.msgKey.flags & RSSL_MKF_HAS_NAME_TYPE )
	{
		_rsslRdmLoginRequest.userNameType = pRsslRequestMsg->msgBase.msgKey.nameType;
		_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_USERNAME_TYPE;
	}
	else
		_rsslRdmLoginRequest.flags &= ~RDM_LG_RQF_HAS_USERNAME_TYPE;

	if ( pRsslRequestMsg->msgBase.msgKey.flags & RSSL_MKF_HAS_NAME )
	{
		_username.set( pRsslRequestMsg->msgBase.msgKey.name.data, pRsslRequestMsg->msgBase.msgKey.name.length );
		_rsslRdmLoginRequest.userName.data = (char*) _username.c_str();
		_rsslRdmLoginRequest.userName.length = _username.length();
	}
	else
	{
		_username.clear();
		rsslClearBuffer( &_rsslRdmLoginRequest.userName );
	}

	if ( ( pRsslRequestMsg->msgBase.msgKey.flags & RSSL_MKF_HAS_ATTRIB ) &&
		pRsslRequestMsg->msgBase.msgKey.attribContainerType == RSSL_DT_ELEMENT_LIST )
	{
		RsslDecodeIterator dIter;

		rsslClearDecodeIterator( &dIter );

		RsslRet retCode = rsslSetDecodeIteratorRWFVersion( &dIter, RSSL_RWF_MAJOR_VERSION, RSSL_RWF_MINOR_VERSION );
		if ( retCode != RSSL_RET_SUCCESS )
		{
			EmaString temp( "Internal error. Failed to set RsslDecodeIterator's version in LoginRdmReqMsg::set(). Attributes will be skipped." );
			_emaConfigImpl.appendConfigError( temp, OmmLoggerClient::ErrorEnum );
			return *this;
		}

		retCode = rsslSetDecodeIteratorBuffer( &dIter, &pRsslRequestMsg->msgBase.msgKey.encAttrib );
		if ( retCode != RSSL_RET_SUCCESS )
		{
			EmaString temp( "Internal error. Failed to set RsslDecodeIterator's Buffer in LoginRdmReqMsg::set(). Attributes will be skipped." );
			_emaConfigImpl.appendConfigError( temp, OmmLoggerClient::ErrorEnum );
			return *this;
		}

		RsslElementList elementList;
		rsslClearElementList( &elementList );
		retCode = rsslDecodeElementList( &dIter, &elementList, 0 );
		if ( retCode != RSSL_RET_SUCCESS )
		{
			if ( retCode != RSSL_RET_NO_DATA )
			{
				EmaString temp( "Internal error while decoding element list containing login attributes. Error='" );
				temp.append( rsslRetCodeToString( retCode ) ).append( "'. Attributes will be skipped." );
				_emaConfigImpl.appendConfigError( temp, OmmLoggerClient::ErrorEnum );
				return *this;
			}

			return *this;
		}

		RsslElementEntry elementEntry;
		rsslClearElementEntry( &elementEntry );

		retCode = rsslDecodeElementEntry( &dIter, &elementEntry );

		while ( retCode != RSSL_RET_END_OF_CONTAINER )
		{
			if ( retCode != RSSL_RET_SUCCESS )
			{
				EmaString temp( "Internal error while decoding element entry with a login attribute. Error='" );
				temp.append( rsslRetCodeToString( retCode ) ).append( "'. Attribute will be skipped." );
				_emaConfigImpl.appendConfigError( temp, OmmLoggerClient::WarningEnum );
				continue;
			}

			if ( rsslBufferIsEqual( &elementEntry.name, &RSSL_ENAME_SINGLE_OPEN ) )
			{
				retCode = rsslDecodeUInt( &dIter, &_rsslRdmLoginRequest.singleOpen );
				if ( retCode != RSSL_RET_SUCCESS )
				{
					EmaString temp( "Internal error while decoding login attribute of single open. Error='" );
					temp.append( rsslRetCodeToString( retCode ) ).append( "'. Attribute will be skipped." );
					_emaConfigImpl.appendConfigError( temp, OmmLoggerClient::WarningEnum );
					continue;
				}

				_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_SINGLE_OPEN;
			}
			else if ( rsslBufferIsEqual( &elementEntry.name, &RSSL_ENAME_ALLOW_SUSPECT_DATA ) )
			{
				retCode = rsslDecodeUInt( &dIter, &_rsslRdmLoginRequest.allowSuspectData );
				if ( retCode != RSSL_RET_SUCCESS )
				{
					EmaString temp( "Internal error while decoding login attribute of allow suspect data. Error='" );
					temp.append( rsslRetCodeToString( retCode ) ).append( "'. Attribute will be skipped." );
					_emaConfigImpl.appendConfigError( temp, OmmLoggerClient::WarningEnum );
					continue;
				}

				_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_ALLOW_SUSPECT_DATA;
			}
			else if ( rsslBufferIsEqual( &elementEntry.name, &RSSL_ENAME_APPID ) )
			{
				retCode = rsslDecodeBuffer( &dIter, &_rsslRdmLoginRequest.applicationId );
				if ( retCode != RSSL_RET_SUCCESS )
				{
					EmaString temp( "Internal error while decoding login attribute of application id. Error='" );
					temp.append( rsslRetCodeToString( retCode ) ).append( "'. Attribute will be skipped." );
					_emaConfigImpl.appendConfigError( temp, OmmLoggerClient::WarningEnum );
					continue;
				}

				_applicationId.set( _rsslRdmLoginRequest.applicationId.data, _rsslRdmLoginRequest.applicationId.length );
				_rsslRdmLoginRequest.applicationId.data = (char*) _applicationId.c_str();
				_rsslRdmLoginRequest.applicationId.length = _applicationId.length();
				_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_APPLICATION_ID;
			}
			else if ( rsslBufferIsEqual( &elementEntry.name, &RSSL_ENAME_APPNAME ) )
			{
				retCode = rsslDecodeBuffer( &dIter, &_rsslRdmLoginRequest.applicationName );
				if ( retCode != RSSL_RET_SUCCESS )
				{
					EmaString temp( "Internal error while decoding login attribute of application name. Error='" );
					temp.append( rsslRetCodeToString( retCode ) ).append( "'. Attribute will be skipped." );
					_emaConfigImpl.appendConfigError( temp, OmmLoggerClient::WarningEnum );
					continue;
				}

				_applicationName.set( _rsslRdmLoginRequest.applicationName.data, _rsslRdmLoginRequest.applicationName.length );
				_rsslRdmLoginRequest.applicationName.data = (char*) _applicationName.c_str();
				_rsslRdmLoginRequest.applicationName.length = _applicationName.length();
				_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_APPLICATION_NAME;
			}
			else if ( rsslBufferIsEqual( &elementEntry.name, &RSSL_ENAME_POSITION ) )
			{
				retCode = rsslDecodeBuffer( &dIter, &_rsslRdmLoginRequest.position );
				if ( retCode != RSSL_RET_SUCCESS )
				{
					EmaString temp( "Internal error while decoding login attribute of position. Error='" );
					temp.append( rsslRetCodeToString( retCode ) ).append( "'. Attribute will be skipped." );
					_emaConfigImpl.appendConfigError( temp, OmmLoggerClient::WarningEnum );
					continue;
				}

				_position.set( _rsslRdmLoginRequest.position.data, _rsslRdmLoginRequest.position.length );
				_rsslRdmLoginRequest.position.data = (char*) _position.c_str();
				_rsslRdmLoginRequest.position.length = _position.length();
				_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_POSITION;
			}
			else if ( rsslBufferIsEqual( &elementEntry.name, &RSSL_ENAME_PASSWORD ) )
			{
				retCode = rsslDecodeBuffer( &dIter, &_rsslRdmLoginRequest.password );
				if ( retCode != RSSL_RET_SUCCESS )
				{
					EmaString temp( "Internal error while decoding login attribute of password. Error='" );
					temp.append( rsslRetCodeToString( retCode ) ).append( "'. Attribute will be skipped." );
					_emaConfigImpl.appendConfigError( temp, OmmLoggerClient::WarningEnum );
					continue;
				}

				_password.set( _rsslRdmLoginRequest.password.data, _rsslRdmLoginRequest.password.length );
				_rsslRdmLoginRequest.password.data = (char*) _password.c_str();
				_rsslRdmLoginRequest.password.length = _password.length();
				_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_PASSWORD;
			}
			else if ( rsslBufferIsEqual( &elementEntry.name, &RSSL_ENAME_PROV_PERM_PROF ) )
			{
				retCode = rsslDecodeUInt( &dIter, &_rsslRdmLoginRequest.providePermissionProfile );
				if ( retCode != RSSL_RET_SUCCESS )
				{
					EmaString temp( "Internal error while decoding login attribute of provide permission profile. Error='" );
					temp.append( rsslRetCodeToString( retCode ) ).append( "'. Attribute will be skipped." );
					_emaConfigImpl.appendConfigError( temp, OmmLoggerClient::WarningEnum );
					continue;
				}

				_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_PROVIDE_PERM_PROFILE;
			}
			else if ( rsslBufferIsEqual( &elementEntry.name, &RSSL_ENAME_PROV_PERM_EXP ) )
			{
				retCode = rsslDecodeUInt( &dIter, &_rsslRdmLoginRequest.providePermissionExpressions );
				if ( retCode != RSSL_RET_SUCCESS )
				{
					EmaString temp( "Internal error while decoding login attribute of provide permission expressions. Error='" );
					temp.append( rsslRetCodeToString( retCode ) ).append( "'. Attribute will be skipped." );
					_emaConfigImpl.appendConfigError( temp, OmmLoggerClient::WarningEnum );
					continue;
				}

				_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_PROVIDE_PERM_EXPR;
			}
			else if ( rsslBufferIsEqual( &elementEntry.name, &RSSL_ENAME_SUPPORT_PROVIDER_DICTIONARY_DOWNLOAD ) )
			{
				retCode = rsslDecodeUInt( &dIter, &_rsslRdmLoginRequest.supportProviderDictionaryDownload );
				if ( retCode != RSSL_RET_SUCCESS )
				{
					EmaString temp( "Internal error while decoding login attribute of support provider dictionary download. Error='" );
					temp.append( rsslRetCodeToString( retCode ) ).append( "'. Attribute will be skipped." );
					_emaConfigImpl.appendConfigError( temp, OmmLoggerClient::WarningEnum );
					continue;
				}

				_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_SUPPORT_PROV_DIC_DOWNLOAD;
			}
			else if ( rsslBufferIsEqual( &elementEntry.name, &RSSL_ENAME_DOWNLOAD_CON_CONFIG ) )
			{
				retCode = rsslDecodeUInt( &dIter, &_rsslRdmLoginRequest.downloadConnectionConfig );
				if ( retCode != RSSL_RET_SUCCESS )
				{
					EmaString temp( "Internal error while decoding login attribute of download connection configure. Error='" );
					temp.append( rsslRetCodeToString( retCode ) ).append( "'. Attribute will be skipped." );
					_emaConfigImpl.appendConfigError( temp, OmmLoggerClient::WarningEnum );
					continue;
				}

				_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_DOWNLOAD_CONN_CONFIG;
			}
			else if ( rsslBufferIsEqual( &elementEntry.name, &RSSL_ENAME_INST_ID ) )
			{
				retCode = rsslDecodeBuffer( &dIter, &_rsslRdmLoginRequest.instanceId );
				if ( retCode != RSSL_RET_SUCCESS )
				{
					EmaString temp( "Internal error while decoding login attribute of instance Id. Error='" );
					temp.append( rsslRetCodeToString( retCode ) ).append( "'. Attribute will be skipped." );
					_emaConfigImpl.appendConfigError( temp, OmmLoggerClient::WarningEnum );
					continue;
				}

				_instanceId.set( _rsslRdmLoginRequest.instanceId.data, _rsslRdmLoginRequest.instanceId.length );
				_rsslRdmLoginRequest.instanceId.data = (char*) _instanceId.c_str();
				_rsslRdmLoginRequest.instanceId.length = _instanceId.length();
				_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_INSTANCE_ID;
			}
			else if ( rsslBufferIsEqual( &elementEntry.name, &RSSL_ENAME_ROLE ) )
			{
				retCode = rsslDecodeUInt( &dIter, &_rsslRdmLoginRequest.role );
				if ( retCode != RSSL_RET_SUCCESS )
				{
					EmaString temp( "Internal error while decoding login attribute of role. Error='" );
					temp.append( rsslRetCodeToString( retCode ) ).append( "'. Attribute will be skipped." );
					_emaConfigImpl.appendConfigError( temp, OmmLoggerClient::WarningEnum );
					continue;
				}

				_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_ROLE;
			}

			retCode = rsslDecodeElementEntry( &dIter, &elementEntry );
		}
	}

	return *this;
}

RsslRDMLoginRequest* LoginRdmReqMsg::get()
{
	return &_rsslRdmLoginRequest;
}

LoginRdmReqMsg& LoginRdmReqMsg::username( const EmaString& value )
{
	_username = value;
	_rsslRdmLoginRequest.userName.data = (char*) _username.c_str();
	_rsslRdmLoginRequest.userName.length = _username.length();
	return *this;
}

LoginRdmReqMsg& LoginRdmReqMsg::position( const EmaString& value )
{
	_position = value;
	_rsslRdmLoginRequest.position.data = (char*) _position.c_str();
	_rsslRdmLoginRequest.position.length = _position.length();
	_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_POSITION;
	return *this;
}

LoginRdmReqMsg& LoginRdmReqMsg::password( const EmaString& value )
{
	_password = value;
	_rsslRdmLoginRequest.password.data = (char*) _password.c_str();
	_rsslRdmLoginRequest.password.length = _password.length();
	_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_PASSWORD;
	return *this;
}

LoginRdmReqMsg& LoginRdmReqMsg::applicationId( const EmaString& value )
{
	_applicationId = value;
	_rsslRdmLoginRequest.applicationId.data = (char*) _applicationId.c_str();
	_rsslRdmLoginRequest.applicationId.length = _applicationId.length();
	_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_APPLICATION_ID;
	return *this;
}

LoginRdmReqMsg& LoginRdmReqMsg::applicationName( const EmaString& value )
{
	_applicationName = value;
	_rsslRdmLoginRequest.applicationName.data = (char*) _applicationName.c_str();
	_rsslRdmLoginRequest.applicationName.length = _applicationName.length();
	_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_APPLICATION_NAME;
	return *this;
}

LoginRdmReqMsg& LoginRdmReqMsg::instanceId( const EmaString& value )
{
	_instanceId = value;
	_rsslRdmLoginRequest.instanceId.data = (char*) _instanceId.c_str();
	_rsslRdmLoginRequest.instanceId.length = _instanceId.length();
	_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_INSTANCE_ID;
	return *this;
}

LoginRdmReqMsg& LoginRdmReqMsg::setRole( RDMLoginRoleTypes role )
{
	_rsslRdmLoginRequest.role = role;
	_rsslRdmLoginRequest.flags |= RDM_LG_RQF_HAS_ROLE;
	return *this;
}
