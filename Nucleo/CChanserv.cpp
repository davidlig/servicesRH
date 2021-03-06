/////////////////////////////////////////////////////////////
//
// Servicios de redhispana.org
// Est� prohibida la reproducci�n y el uso parcial o total
// del c�digo fuente de estos servicios sin previa
// autorizaci�n escrita del autor de estos servicios.
//
// Si usted viola esta licencia se emprender�n acciones legales.
//
// (C) RedHispana.Org 2009
//
// Archivo:     CChanserv.cpp
// Prop�sito:   Registro de canales
// Autores:     Alberto Alonso <rydencillo@gmail.com>
//

#include "stdafx.h"

const char* const CChanserv::ms_szValidLevels [] = {
    "autoop", "autohalfop", "autovoice", "autodeop", "autodehalfop", "autodevoice", "nojoin",
    "invite", "akick", "set", "clear", "unban", "opdeop", "halfopdehalfop", "voicedevoce",
    "acc-list", "acc-change"
};


CChanserv::CChanserv ( const CConfig& config )
: CService ( "chanserv", config ),
  m_bEOBAcked ( false )
{
    // Registramos los comandos
#define REGISTER(x,ver) RegisterCommand ( #x, COMMAND_CALLBACK ( &CChanserv::cmd ## x , this ), COMMAND_CALLBACK ( &CChanserv::verify ## ver , this ) )
    REGISTER ( Help,        All );
    REGISTER ( Register,    All );
    REGISTER ( Identify,    All );
    REGISTER ( Levels,      All );
    REGISTER ( Access,      All );

    REGISTER ( Owner,       All );
    REGISTER ( Deowner,     All );
    REGISTER ( Op,          All );
    REGISTER ( Deop,        All );
    REGISTER ( Halfop,      All );
    REGISTER ( Dehalfop,    All );
    REGISTER ( Voice,       All );
    REGISTER ( Devoice,     All );
#undef REGISTER

    // Cargamos la configuraci�n para nickserv
#define SAFE_LOAD(dest,section,var) do { \
    if ( !config.GetValue ( (dest), (section), (var) ) ) \
    { \
        SetError ( CString ( "No se pudo leer la variable '%s' de la configuraci�n.", (var) ) ); \
        SetOk ( false ); \
        return; \
    } \
} while ( 0 )

    CString szTemp;

    // Lista de acceso
    SAFE_LOAD ( szTemp, "options.chanserv", "access.maxList" );
    m_options.uiMaxAccessList = static_cast < unsigned int > ( strtoul ( szTemp, NULL, 10 ) );

    // L�mites de tiempo
    SAFE_LOAD ( szTemp, "options.chanserv", "time.register" );
    m_options.uiTimeRegister = static_cast < unsigned int > ( strtoul ( szTemp, NULL, 10 ) );
#undef SAFE_LOAD

    // Obtenemos el servicio nickserv
    m_pNickserv = dynamic_cast < CNickserv* > ( CService::GetService ( "nickserv" ) );
    if ( !m_pNickserv )
    {
        SetError ( "No se pudo obtener el servicio nickserv" );
        SetOk ( false );
        return;
    }
}

CChanserv::~CChanserv ( )
{
    Unload ();
}


void CChanserv::Load ()
{
    if ( !IsLoaded () )
    {
        // Registramos los eventos
        CProtocol& protocol = CProtocol::GetSingleton ();
        protocol.AddHandler ( CMessageJOIN (), PROTOCOL_CALLBACK ( &CChanserv::evtJoin, this ) );
        protocol.AddHandler ( CMessageMODE (), PROTOCOL_CALLBACK ( &CChanserv::evtMode, this ) );
        protocol.AddHandler ( CMessageIDENTIFY (), PROTOCOL_CALLBACK ( &CChanserv::evtIdentify, this ) );
        protocol.AddHandler ( CMessageNICK (), PROTOCOL_CALLBACK ( &CChanserv::evtNick, this ) );
        protocol.AddHandler ( CMessageEOB_ACK (), PROTOCOL_CALLBACK ( &CChanserv::evtEOBAck, this ) );

        CService::Load ();
    }
}

void CChanserv::Unload ()
{
    if ( IsLoaded () )
    {
        // Desregistramos los eventos
        CProtocol& protocol = CProtocol::GetSingleton ();
        protocol.RemoveHandler ( CMessageJOIN (), PROTOCOL_CALLBACK ( &CChanserv::evtJoin, this ) );
        protocol.RemoveHandler ( CMessageMODE (), PROTOCOL_CALLBACK ( &CChanserv::evtMode, this ) );
        protocol.RemoveHandler ( CMessageIDENTIFY (), PROTOCOL_CALLBACK ( &CChanserv::evtIdentify, this ) );
        protocol.RemoveHandler ( CMessageNICK (), PROTOCOL_CALLBACK ( &CChanserv::evtNick, this ) );
        protocol.RemoveHandler ( CMessageEOB_ACK (), PROTOCOL_CALLBACK ( &CChanserv::evtEOBAck, this ) );

        CService::Unload ();
    }
}

void CChanserv::SetupForCommand ( CUser& user )
{
    SServicesData& data = user.GetServicesData ();
    data.chanAccess.bCached = false;

    CService::SetupForCommand ( user );
}

bool CChanserv::CheckIdentifiedAndReg ( CUser& s )
{
    SServicesData& data = s.GetServicesData ();

    if ( data.ID == 0ULL )
    {
        LangMsg ( s, "NOT_REGISTERED", m_pNickserv->GetName ().c_str () );
        return false;
    }

    if ( data.bIdentified == false )
    {
        LangMsg ( s, "NOT_IDENTIFIED", m_pNickserv->GetName ().c_str () );
        return false;
    }

    return true;
}

unsigned long long CChanserv::GetChannelID ( const CString& szChannelName )
{
    // Generamos la consulta SQL para obtener el ID de un canal
    static CDBStatement* SQLGetChannelID = 0;
    if ( !SQLGetChannelID )
    {
        SQLGetChannelID = CDatabase::GetSingleton ().PrepareStatement (
              "SELECT id FROM channel WHERE name=?"
            );
        if ( !SQLGetChannelID )
        {
            ReportBrokenDB ( 0, 0, "Generando chanserv.SQLGetChannelID" );
            return 0ULL;
        }
        else
            SQLGetChannelID->AddRef ( &SQLGetChannelID );
    }

    // La ejecutamos
    if ( ! SQLGetChannelID->Execute ( "s", szChannelName.c_str () ) )
    {
        ReportBrokenDB ( 0, SQLGetChannelID, "Ejecutando chanserv.SQLGetChannelID" );
        return 0ULL;
    }

    // Obtenemos el ID
    unsigned long long ID;
    if ( SQLGetChannelID->Fetch ( 0, 0, "Q", &ID ) != CDBStatement::FETCH_OK )
        ID = 0ULL;
    SQLGetChannelID->FreeResult ();

    return ID;
}


CChannel* CChanserv::GetChannel ( CUser& s, const CString& szChannelName )
{
    CChannel* pChannel = CChannelManager::GetSingleton ().GetChannel ( szChannelName );
    if ( !pChannel )
        LangMsg ( s, "CHANNEL_NOT_FOUND", szChannelName.c_str () );
    return pChannel;
}

CChannel* CChanserv::GetRegisteredChannel ( CUser& s, const CString& szChannelName, unsigned long long& ID, bool bAllowUnregistered )
{
    CChannel* pChannel = GetChannel ( s, szChannelName );
    if ( pChannel )
    {
        ID = GetChannelID ( szChannelName );
        if ( bAllowUnregistered || ID != 0ULL )
            return pChannel;
        LangMsg ( s, "CHANNEL_NOT_REGISTERED", szChannelName.c_str () );
    }

    return NULL;
}

bool CChanserv::HasChannelDebug ( unsigned long long ID )
{
    // Preparamos la consulta para verificar si un canal tiene debug activo
    static CDBStatement* SQLHasDebug = 0;
    if ( !SQLHasDebug )
    {
        SQLHasDebug = CDatabase::GetSingleton ().PrepareStatement (
              "SELECT debug FROM channel WHERE id=?"
            );
        if ( !SQLHasDebug )
            return ReportBrokenDB ( 0, 0, "Generando chanserv.SQLHasDebug" );
        else
            SQLHasDebug->AddRef ( &SQLHasDebug );
    }

    // Ejecutamos la consulta
    if ( !SQLHasDebug->Execute ( "Q", ID ) )
        return ReportBrokenDB ( 0, SQLHasDebug, "Ejecutando chanserv.SQLHasDebug" );

    // Obtenemos el resultado
    char szDebug [ 4 ];
    if ( SQLHasDebug->Fetch ( 0, 0, "s", szDebug, sizeof ( szDebug ) ) != CDBStatement::FETCH_OK )
    {
        SQLHasDebug->FreeResult ();
        return false;
    }
    SQLHasDebug->FreeResult ();

    return ( *szDebug == 'Y' );
}


int CChanserv::GetAccess ( CUser& s, unsigned long long ID, bool bCheckFounder )
{
    SServicesData& data = s.GetServicesData ();

    // Si no est� registrado o identificado, el nivel siempre ser� 0
    if ( data.ID == 0ULL || data.bIdentified == false )
        return 0;

    if ( data.chanAccess.bCached == false )
    {
        int iLevel = GetAccess ( data.ID, ID, bCheckFounder, &s );
        data.chanAccess.bCached = true;
        data.chanAccess.iLevel = iLevel;
    }

    return data.chanAccess.iLevel;
}

int CChanserv::GetAccess ( unsigned long long AccountID, unsigned long long ID, bool bCheckFounder, CUser* pUser )
{
    // Generamos la consulta para comprobar si el usuario es el fundador del canal
    static CDBStatement* SQLCheckFounder = 0;
    if ( !SQLCheckFounder )
    {
        SQLCheckFounder = CDatabase::GetSingleton ().PrepareStatement (
              "SELECT founder FROM channel WHERE id=? AND founder=?"
            );
        if ( !SQLCheckFounder )
        {
            ReportBrokenDB ( pUser, 0, "Generando chanserv.SQLCheckFounder" );
            return 0;
        }
        else
            SQLCheckFounder->AddRef ( &SQLCheckFounder );
    }

    // Generamos la consulta para obtener el nivel de acceso que tiene el usuario en el canal
    static CDBStatement* SQLGetAccess = 0;
    if ( !SQLGetAccess )
    {
        SQLGetAccess = CDatabase::GetSingleton ().PrepareStatement (
              "SELECT `level` FROM access WHERE account=? AND channel=?"
            );
        if ( !SQLGetAccess )
        {
            ReportBrokenDB ( pUser, 0, "Generando chanserv.SQLGetAccess" );
            return 0;
        }
        else
            SQLGetAccess->AddRef ( &SQLGetAccess );
    }

    // Primero comprobamos si est� identificado como fundador
    if ( pUser && bCheckFounder )
    {
        SServicesData& data = pUser->GetServicesData ();
        if ( data.vecChannelFounder.size () > 0 )
        {
            for ( std::vector < unsigned long long >::const_iterator i = data.vecChannelFounder.begin ();
                  i != data.vecChannelFounder.end ();
                  ++i )
            {
                if ( (*i) == ID )
                    return 500;
            }
        }
    }

    // Comprobamos si es el fundador del canal
    if ( bCheckFounder )
    {
        if ( !SQLCheckFounder->Execute ( "QQ", ID, AccountID ) )
        {
            ReportBrokenDB ( pUser, SQLCheckFounder, "Ejecutando chanserv.SQLCheckFounder" );
            return 0;
        }

        unsigned long long FounderID;
        if ( SQLCheckFounder->Fetch ( 0, 0, "Q", &FounderID ) == CDBStatement::FETCH_OK && FounderID == AccountID )
        {
            SQLCheckFounder->FreeResult ();
            return 500;
        }
        SQLCheckFounder->FreeResult ();
    }

    // Comprobamos el acceso que tenga en el canal
    if ( !SQLGetAccess->Execute ( "QQ", AccountID, ID ) )
    {
        ReportBrokenDB ( pUser, SQLGetAccess, "Ejecutando chanserv.SQLGetAccess" );
        return 0;
    }

    int iAccess;
    if ( SQLGetAccess->Fetch ( 0, 0, "d", &iAccess ) != CDBStatement::FETCH_OK )
        iAccess = 0;
    SQLGetAccess->FreeResult ();

    return iAccess;
}


int CChanserv::GetLevel ( unsigned long long ID, EChannelLevel level )
{
    // Preparamos el array de consultas SQL para cada uno de los niveles
    static CDBStatement* s_statements [ LEVEL_MAX ] = { NULL };

    if ( s_statements [ 0 ] == NULL )
    {
        for ( unsigned int i = 0;
              i < sizeof ( ms_szValidLevels ) / sizeof ( ms_szValidLevels [ 0 ] );
              ++i )
        {
            const char* szCurrent = ms_szValidLevels [ i ];
            CString szQuery ( "SELECT `%s` FROM levels WHERE channel=?", szCurrent );
            CDBStatement* SQLGetLevel = CDatabase::GetSingleton ().PrepareStatement ( szQuery );
            if ( !SQLGetLevel )
            {
                s_statements [ i ] = NULL;
                ReportBrokenDB ( 0, 0, "Generando chanserv.SQLGetLevel" );
            }
            else
            {
                s_statements [ i ] = SQLGetLevel;
                SQLGetLevel->AddRef ( &( s_statements [ i ]) );
            }
        }
    }

    // Obtenemos la consulta SQL para este nivel concreto
    unsigned int uiLevel = static_cast < unsigned int > ( level );
    CDBStatement* SQLGetLevel = s_statements [ uiLevel ];
    if ( !SQLGetLevel )
        return 0;

    // Ejecutamos la consulta SQL
    if ( !SQLGetLevel->Execute ( "Q", ID ) )
    {
        ReportBrokenDB ( 0, SQLGetLevel, "Ejecutando chanserv.SQLGetLevel" );
        return 0;
    }

    // Obtenemos el nivel
    int iRequiredLevel;
    if ( SQLGetLevel->Fetch ( 0, 0, "d", &iRequiredLevel ) != CDBStatement::FETCH_OK )
        iRequiredLevel = 0;
    SQLGetLevel->FreeResult ();

    return iRequiredLevel;
}


bool CChanserv::CheckAccess ( CUser& user, unsigned long long ID, EChannelLevel level )
{
    int iRequiredLevel = GetLevel ( ID, level );
    int iAccess = GetAccess ( user, ID );

    if ( iRequiredLevel < 0 )
        return ( iRequiredLevel == iAccess );
    else
        return ( iRequiredLevel <= iAccess );
}



void CChanserv::CheckOnjoinStuff ( CUser& user, CChannel& channel, bool bSendGreetmsg )
{
    if ( !m_bEOBAcked )
    {
        // Si a�n no hemos terminado de recibir la DDB, lo encolamos.
        SJoinProcessQueue queue;
        queue.pChannel = &channel;
        queue.pUser = &user;
        m_vecJoinProcessQueue.push_back ( queue );
        return;
    }

    SServicesData& data = user.GetServicesData ();
    CMembership* pMembership = channel.GetMembership ( &user );

    // Limpiamos la cach� de registro
    data.chanAccess.bCached = false;

    char szNumeric [ 8 ];
    user.FormatNumeric ( szNumeric );

    // Comprobamos que el canal est� registrado
    unsigned long long ID = GetChannelID ( channel.GetName () );
    if ( ID != 0ULL )
    {
        // Eliminamos la cach� de acceso
        data.chanAccess.bCached = false;

        // Obtenemos su nivel de acceso
        int iAccess = GetAccess ( user, ID );
        if ( iAccess == 500 )
        {
            // Fundador
            if ( pMembership &&
                 ( pMembership->GetFlags () & ( CChannel::CFLAG_OWNER | CChannel::CFLAG_OP ) ) !=
                   ( CChannel::CFLAG_OWNER | CChannel::CFLAG_OP )
               )
            {
                BMode ( &channel, "+qo", szNumeric, szNumeric );
            }
        }

        // Comprobamos si se le permite entrar en el canal
        if ( CheckAccess ( user, ID, LEVEL_NOJOIN ) )
        {
            CString szReason;
            GetLangTopic ( szReason, "", "NOJOIN_KICK_REASON" );
            while ( szReason.at ( szReason.length () - 1 ) == '\r' ||
                    szReason.at ( szReason.length () - 1 ) == '\n' )
            {
                szReason.resize ( szReason.length () - 1 );
            }               
            KickBan ( &user, &channel, szReason, BAN_HOST );
        }
        else
        {
            // Comprobamos los otros flags de canal
            unsigned char ucMode = 0;
            if ( CheckAccess ( user, ID, LEVEL_AUTOOP ) )
                ucMode = 'o';
            else if ( CheckAccess ( user, ID, LEVEL_AUTOHALFOP ) )
                ucMode = 'h';
            else if ( CheckAccess ( user, ID, LEVEL_AUTOVOICE ) )
                ucMode = 'v';

            if ( ucMode != 0 )
            {
                unsigned long ulFlag = CChannel::GetModeFlag ( ucMode );
                if ( pMembership && ( pMembership->GetFlags () & ulFlag ) == 0 )
                {
                    CString szFlag ( "+%c", ucMode );
                    Mode ( &channel, szFlag, szNumeric );
                }
            }
        }

        // Greetmsg
        if ( bSendGreetmsg && iAccess > 0 )
        {
            // Generamos la consulta para obtener el greetmsg
            static CDBStatement* SQLGetGreetmsg = 0;
            if ( !SQLGetGreetmsg )
            {
                SQLGetGreetmsg = CDatabase::GetSingleton ().PrepareStatement (
                      "SELECT greetmsg FROM account WHERE id=?"
                    );
                if ( !SQLGetGreetmsg )
                {
                    ReportBrokenDB ( &user, 0, "Generando chanserv.SQLGetGreetmsg" );
                    return;
                }
                else
                    SQLGetGreetmsg->AddRef ( &SQLGetGreetmsg );
            }

            // Ejecutamos la consulta
            if ( ! SQLGetGreetmsg->Execute ( "Q", data.ID ) )
            {
                ReportBrokenDB ( &user, SQLGetGreetmsg, "Ejecutando chanserv.SQLGetGreetmsg" );
                return;
            }

            // Obtenemos el greetmsg
            char szGreetmsg [ 512 ];
            bool bNull;
            if ( SQLGetGreetmsg->Fetch ( 0, &bNull, "s", szGreetmsg, sizeof ( szGreetmsg ) ) == CDBStatement::FETCH_OK )
            {
                if ( !bNull )
                    LangMsg ( channel, "GREETMSG", user.GetName ().c_str (), szGreetmsg );
            }
            SQLGetGreetmsg->FreeResult ();
        }
    }
}


static inline void ClearPassword ( CString& szPassword )
{
#ifdef WIN32
    char* szCopy = (char*)_alloca ( szPassword.length () + 1 );
#else
    char szCopy [ szPassword.length () + 1 ];
#endif

    memset ( szCopy, 'A', szPassword.length () );
    szCopy [ szPassword.length () ] = '\0';

    szPassword.assign ( szCopy );
}


///////////////////////////////////////////////////
////                 COMANDOS                  ////
///////////////////////////////////////////////////
void CChanserv::UnknownCommand ( SCommandInfo& info )
{
    info.ResetParamCounter ();
    LangMsg ( *( info.pSource ), "UNKNOWN_COMMAND", info.GetNextParam ().c_str () );
}

#define COMMAND(x) bool CChanserv::cmd ## x ( SCommandInfo& info )

///////////////////
// HELP
//
COMMAND(Help)
{
    bool bRet = CService::ProcessHelp ( info );

    if ( bRet )
    {
        CUser& s = *( info.pSource );
        info.ResetParamCounter ();
        info.GetNextParam ();
        CString& szTopic = info.GetNextParam ();

        if ( szTopic == "" )
        {
            if ( HasAccess ( s, RANK_PREOPERATOR ) )
            {
                LangMsg ( s, "PREOPERS_HELP" );
                if ( HasAccess ( s, RANK_OPERATOR ) )
                {
                    LangMsg ( s, "OPERS_HELP" );
                    if ( HasAccess ( s, RANK_COADMINISTRATOR ) )
                    {
                        LangMsg ( s, "COADMINS_HELP" );
                        if ( HasAccess ( s, RANK_ADMINISTRATOR ) )
                            LangMsg ( s, "ADMINS_HELP" );
                    }
                }
            }
        }
    }

    return bRet;
}


///////////////////
// REGISTER
//
COMMAND(Register)
{
    CUser& s = *( info.pSource );
    SServicesData& data = s.GetServicesData ();

    // Generamos la consulta SQL para registrar canales
    static CDBStatement* SQLRegister = 0;
    if ( !SQLRegister )
    {
        SQLRegister = CDatabase::GetSingleton ().PrepareStatement (
              "INSERT INTO channel ( name, password, description, registered, lastUsed, founder ) "
              "VALUES ( ?, MD5(?), ?, ?, ?, ? )"
            );
        if ( !SQLRegister )
            return ReportBrokenDB ( &s, 0, "Generando chanserv.SQLRegister" );
        else
            SQLRegister->AddRef ( &SQLRegister );
    }

    // Generamos la consulta SQL para crear el registro de niveles
    static CDBStatement* SQLCreateLevels = 0;
    if ( !SQLCreateLevels )
    {
        SQLCreateLevels = CDatabase::GetSingleton ().PrepareStatement (
              "INSERT INTO levels ( channel ) VALUES ( ? )"
            );
        if ( !SQLCreateLevels )
            return ReportBrokenDB ( &s, 0, "Generando chanserv.SQLCreateLevels" );
        else
            SQLCreateLevels->AddRef ( &SQLCreateLevels );
    }

    // Nos aseguramos de que est� identificado
    if ( !CheckIdentifiedAndReg ( s ) )
        return false;

    // Obtenemos el canal a registrar
    CString& szChannel = info.GetNextParam ();
    if ( szChannel == "" )
        return SendSyntax ( s, "REGISTER" );

    // Obtenemos la contrase�a
    CString& szPassword = info.GetNextParam ();
    if ( szPassword == "" )
        return SendSyntax ( s, "REGISTER" );

    // Obtenemos la descripci�n
    CString szDesc;
    info.GetRemainingText ( szDesc );
    if ( szDesc == "" )
    {
        ClearPassword ( szPassword );
        return SendSyntax ( s, "REGISTER" );
    }

    // Comprobamos que nos proveen un nombre de canal correcto
    if ( *szChannel != '#' )
    {
        ClearPassword ( szPassword );
        LangMsg ( s, "REGISTER_BAD_NAME", szChannel.c_str () );
        return false;
    }

    // Comprobamos que el canal no est� ya registrado
    unsigned long long ID = GetChannelID ( szChannel );
    if ( ID != 0ULL )
    {
        ClearPassword ( szPassword );
        LangMsg ( s, "REGISTER_ALREADY_REGISTERED", szChannel.c_str () );
        return false;
    }

    // Comprobamos que el canal exista
    CChannel* pChannel = CChannelManager::GetSingleton ().GetChannel ( szChannel );
    if ( !pChannel )
    {
        ClearPassword ( szPassword );
        LangMsg ( s, "REGISTER_CHANNEL_DOESNT_EXIST", szChannel.c_str () );
        return false;
    }
    CChannel& channel = *pChannel;

    // Comprobamos que el usuario est� en el canal
    CMembership* pMembership = channel.GetMembership ( &s );
    if ( !pMembership )
    {
        ClearPassword ( szPassword );
        LangMsg ( s, "REGISTER_YOU_ARE_NOT_ON_CHANNEL", szChannel.c_str () );
        return false;
    }
    CMembership& membership = *pMembership;

    // Comprobamos que sea operador del canal a registrar
    if ( ! ( membership.GetFlags () & CChannel::CFLAG_OP ) )
    {
        ClearPassword ( szPassword );
        LangMsg ( s, "REGISTER_YOU_ARE_NOT_OPERATOR", szChannel.c_str () );
        return false;
    }

    // Verificamos las restricciones de tiempo
    if ( ! HasAccess ( s, RANK_OPERATOR ) &&
         ! CheckOrAddTimeRestriction ( s, "REGISTER", m_options.uiTimeRegister ) )
        return false;

    // Registramos el canal
    CDate now;
    if ( ! SQLRegister->Execute ( "sssTTQ", channel.GetName ().c_str (),
                                            szPassword.c_str (),
                                            szDesc.c_str (),
                                            &now, &now, data.ID ) )
    {
        ClearPassword ( szPassword );
        return ReportBrokenDB ( &s, SQLRegister, "Ejecutando chanserv.SQLRegister" );
    }
    ID = SQLRegister->InsertID ();
    SQLRegister->FreeResult ();
    ClearPassword ( szPassword );

    if ( ID == 0ULL )
        return ReportBrokenDB ( &s, SQLRegister, "Ejecutando chanserv.SQLRegister" );

    // Generamos el registro en la tabla de niveles
    if ( ! SQLCreateLevels->Execute ( "Q", ID ) )
        return ReportBrokenDB ( &s, SQLCreateLevels, "Ejecutando chanserv.SQLCreateLevels" );
    SQLCreateLevels->FreeResult ();

    // Le establecemos como fundador en la DDB s�lo al nick principal
    CProtocol& protocol = CProtocol::GetSingleton ();
    CString szPrimaryName;
    m_pNickserv->GetAccountName ( data.ID, szPrimaryName );
    protocol.InsertIntoDDB ( 'C', channel.GetName (), CString ( "+founder %s", szPrimaryName.c_str () ) );

    // Si no tiene puesto su nick principal, le damos status de fundador al nick agrupado
    if ( szPrimaryName != s.GetName () )
    {
        char szNumeric [ 8 ];
        s.FormatNumeric ( szNumeric );
        BMode ( pChannel, "+q", szNumeric );
    }

    // Informamos al usuario del registro correcto
    LangMsg ( s, "REGISTER_SUCCESS", channel.GetName ().c_str () );

    // Log
    Log ( "LOG_REGISTER", s.GetName ().c_str (), channel.GetName ().c_str () );

    return true;
}


///////////////////
// IDENTIFY
//
COMMAND(Identify)
{
    CUser& s = *( info.pSource );
    SServicesData& data = s.GetServicesData ();

    // Generamos la consulta para verificar la contrase�a
    static CDBStatement* SQLCheckPassword = 0;
    if ( !SQLCheckPassword )
    {
        SQLCheckPassword = CDatabase::GetSingleton ().PrepareStatement (
              "SELECT id FROM channel WHERE id=? AND password=MD5(?)"
            );
        if ( !SQLCheckPassword )
            return ReportBrokenDB ( &s, 0, "Generando chanserv.SQLCheckPassword" );
        else
            SQLCheckPassword->AddRef ( &SQLCheckPassword );
    }

    // Nos aseguramos de que est� identificado
    if ( !CheckIdentifiedAndReg ( s ) )
        return false;

    // Obtenemos el canal
    CString& szChannel = info.GetNextParam ();
    if ( szChannel == "" )
        return SendSyntax ( s, "IDENTIFY" );

    // Obtenemos la contrase�a
    CString& szPassword = info.GetNextParam ();
    if ( szPassword == "" )
        return SendSyntax ( s, "IDENTIFY" );

    // Buscamos el canal
    unsigned long long ID;
    CChannel* pChannel = GetRegisteredChannel ( s, szChannel, ID );
    if ( !pChannel )
    {
        ClearPassword ( szPassword );
        return false;
    }
    CChannel& channel = *pChannel;

    // Comprobamos si tiene el debug activado
    bool bHasDebug = HasChannelDebug ( ID );

    // Comprobamos la contrase�a
    if ( ! SQLCheckPassword->Execute ( "Qs", ID, szPassword.c_str () ) )
    {
        ClearPassword ( szPassword );
        return ReportBrokenDB ( &s, SQLCheckPassword, "Ejecutando chanserv.SQLCheckPassword" );
    }
    ClearPassword ( szPassword );

    if ( SQLCheckPassword->Fetch ( 0, 0, "Q", &ID ) != CDBStatement::FETCH_OK )
    {
        SQLCheckPassword->FreeResult ();
        LangMsg ( s, "IDENTIFY_WRONG_PASSWORD" );

        if ( bHasDebug )
            LangNotice ( channel, "IDENTIFY_WRONG_PASSWORD_DEBUG", s.GetName ().c_str () );

        m_pNickserv->BadPassword ( s, this );

        // Log
        Log ( "LOG_IDENTIFY_WRONG_PASSWORD", s.GetName ().c_str (), szChannel.c_str () );

        return false;
    }
    SQLCheckPassword->FreeResult ();

    // Le ponemos como fundador
    data.vecChannelFounder.push_back ( ID );
    if ( bHasDebug )
        LangNotice ( channel, "IDENTIFY_SUCCESS_DEBUG", s.GetName ().c_str () );

    // Le damos status de fundador si est� en el canal
    if ( channel.GetMembership ( &s ) )
    {
        char szNumeric [ 8 ];
        s.FormatNumeric ( szNumeric );
        BMode ( pChannel, "+q", szNumeric );
    }

    LangMsg ( s, "IDENTIFY_SUCCESS", channel.GetName ().c_str () );

    // Log
    Log ( "LOG_IDENTIFY", s.GetName ().c_str (), channel.GetName ().c_str () );
    return true;
}


///////////////////
// LEVELS
//
COMMAND(Levels)
{
    CUser& s = *( info.pSource );

    // Preparamos el array de consultas SQL para cada uno de los niveles
    static CDBStatement* s_statements [ LEVEL_MAX ] = { NULL };

    if ( s_statements [ 0 ] == NULL )
    {
        for ( unsigned int i = 0;
              i < sizeof ( ms_szValidLevels ) / sizeof ( ms_szValidLevels [ 0 ] );
              ++i )
        {
            const char* szCurrent = ms_szValidLevels [ i ];
            CString szQuery ( "UPDATE levels SET `%s`=? WHERE channel=?", szCurrent );
            CDBStatement* SQLLevelUpdate = CDatabase::GetSingleton ().PrepareStatement ( szQuery );
            if ( !SQLLevelUpdate )
            {
                s_statements [ i ] = NULL;
                ReportBrokenDB ( 0, 0, "Generando chanserv.SQLLevelUpdate" );
            }
            else
            {
                s_statements [ i ] = SQLLevelUpdate;
                SQLLevelUpdate->AddRef ( &( s_statements [ i ] ) );
            }
        }
    }

    // Generamos la consulta SQL para listar los niveles
    static CDBStatement* SQLListLevels = 0;
    if ( !SQLListLevels )
    {
        // Generamos la consulta
        CString szQuery = "SELECT ";
        for ( unsigned int i = 0;
              i < sizeof ( ms_szValidLevels ) / sizeof ( ms_szValidLevels [ 0 ] );
              ++i )
        {
            const char* szCurrent = ms_szValidLevels [ i ];
            szQuery.append ( CString ( "`%s`,", szCurrent ) );
        }
        szQuery.resize ( szQuery.length () - 1 );
        szQuery.append ( " FROM levels WHERE channel=?" );

        SQLListLevels = CDatabase::GetSingleton ().PrepareStatement ( szQuery );
        if ( !SQLListLevels )
            return ReportBrokenDB ( &s, 0, "Generando chanserv.SQLListLevels" );
        else
            SQLListLevels->AddRef ( &SQLListLevels );
    }

    // Nos aseguramos de que est� identificado
    if ( !CheckIdentifiedAndReg ( s ) )
        return false;

    // Obtenemos el canal
    CString& szChannel = info.GetNextParam ();
    if ( szChannel == "" )
        return SendSyntax ( s, "LEVELS" );

    // Obtenemos el comando
    CString& szCommand = info.GetNextParam ();
    if ( szCommand == "" )
        return SendSyntax ( s, "LEVELS" );

    // Comprobamos que el canal est� registrado
    unsigned long long ID = GetChannelID ( szChannel );
    if ( ID == 0ULL )
    {
        LangMsg ( s, "CHANNEL_NOT_REGISTERED", szChannel.c_str () );
        return false;
    }

    if ( !CPortability::CompareNoCase ( szCommand, "LIST" ) )
    {
        // Listamos los niveles
        if ( !SQLListLevels->Execute ( "Q", ID ) )
            return ReportBrokenDB ( &s, SQLListLevels, "Ejecutando chanserv.SQLListLevels" );

        // Extraemos los niveles
        int iLevels [ LEVEL_MAX ];
        if ( SQLListLevels->Fetch ( 0, 0, "ddddddddddddddddd",
                &iLevels [ 0 ], &iLevels [ 1 ], &iLevels [ 2 ], &iLevels [ 3 ], &iLevels [ 4 ],
                &iLevels [ 5 ], &iLevels [ 6 ], &iLevels [ 7 ], &iLevels [ 8 ], &iLevels [ 9 ],
                &iLevels [ 10 ], &iLevels [ 11 ], &iLevels [ 12 ], &iLevels [ 13 ], &iLevels [ 14 ], 
                &iLevels [ 15 ], &iLevels [ 16 ] ) == CDBStatement::FETCH_OK )
        {
            char szUpperName [ 64 ];
            char szPadding [ 64 ];

            for ( unsigned int i = 0; i < LEVEL_MAX; ++i )
            {
                const char* szCurrent = ms_szValidLevels [ i ];
                unsigned int j;
                for ( j = 0; j < strlen ( szCurrent ); ++j )
                    szUpperName [ j ] = ToUpper ( szCurrent [ j ] );
                szUpperName [ j ] = '\0';

                memset ( szPadding, ' ', 20 - j );
                szPadding [ 20 - j ] = '\0';
                LangMsg ( s, "LEVELS_LIST_ENTRY", szUpperName, szPadding, iLevels [ i ] );
            }
        }

        SQLListLevels->FreeResult ();
    }

    else if ( !CPortability::CompareNoCase ( szCommand, "SET" ) )
    {
        // Comprobamos que sea fundador del canal
        if ( ! HasAccess ( s, RANK_COADMINISTRATOR ) )
        {
            int iAccess = GetAccess ( s, ID );
            if ( iAccess < 500 )
                return AccessDenied ( s );
        }

        // Obtenemos el nombre del nivel
        CString& szLevelName = info.GetNextParam ();
        if ( szLevelName == "" )
            return SendSyntax ( s, "LEVELS" );

        // Obtenemos el valor del nivel
        CString& szLevelValue = info.GetNextParam ();
        if ( szLevelValue == "" )
            return SendSyntax ( s, "LEVELS" );
        int iLevelValue = atoi ( szLevelValue );

        // Comprobamos que el valor del nivel est� en el rango
        if ( iLevelValue < -1 || iLevelValue > 500 )
        {
            LangMsg ( s, "LEVELS_SET_INVALID_VALUE" );
            return false;
        }

        // Obtenemos la posici�n del nivel
        unsigned int i;
        for ( i = 0; i < LEVEL_MAX; ++i )
        {
            if ( !CPortability::CompareNoCase ( ms_szValidLevels [ i ], szLevelName ) )
                break;
        }

        // Comprobamos que hayan proporcionado un nombre de nivel v�lido
        if ( i == LEVEL_MAX )
        {
            LangMsg ( s, "LEVELS_SET_INVALID_NAME" );
            return false;
        }

        // Obtenemos la consulta y la ejecutamos
        CDBStatement* SQLUpdateLevel = s_statements [ i ];
        if ( SQLUpdateLevel )
        {
            if ( !SQLUpdateLevel->Execute ( "dQ", iLevelValue, ID ) )
                return ReportBrokenDB ( &s, SQLUpdateLevel, "Ejecutando chanserv.SQLUpdateLevel" );
            SQLUpdateLevel->FreeResult ();
            LangMsg ( s, "LEVELS_SET_SUCCESS", szLevelName.c_str (), iLevelValue );
        }
    }

    else
        return SendSyntax ( s, "LEVELS" );

    return true;
}



///////////////////
// ACCESS
//
COMMAND(Access)
{
    CUser& s = *( info.pSource );
    SServicesData& data = s.GetServicesData ();

    // Nos aseguramos de que est� identificado
    if ( !CheckIdentifiedAndReg ( s ) )
        return false;

    // Obtenemos el canal
    CString& szChannel = info.GetNextParam ();
    if ( szChannel == "" )
        return SendSyntax ( s, "ACCESS" );

    // Obtenemos el comando
    CString& szCommand = info.GetNextParam ();
    if ( szCommand == "" )
        return SendSyntax ( s, "ACCESS" );

    // Comprobamos que el canal est� registrado
    unsigned long long ID = GetChannelID ( szChannel );
    if ( ID == 0ULL )
    {
        LangMsg ( s, "CHANNEL_NOT_REGISTERED", szChannel.c_str () );
        return false;
    }

    if ( !CPortability::CompareNoCase ( szCommand, "ADD" ) )
    {
        // Generamos la consulta SQL para a�adir accesos
        static CDBStatement* SQLAddAccess = 0;
        if ( !SQLAddAccess )
        {
            SQLAddAccess = CDatabase::GetSingleton ().PrepareStatement (
                  "INSERT INTO access ( account, channel, `level` ) VALUES ( ?, ?, ? )"
                );
            if ( !SQLAddAccess )
                return ReportBrokenDB ( &s, 0, "Generando chanserv.SQLAddAccess" );
            else
                SQLAddAccess->AddRef ( &SQLAddAccess );
        }

        // Generamos la consulta SQL para actualizar accesos
        static CDBStatement* SQLUpdateAccess = 0;
        if ( !SQLUpdateAccess )
        {
            SQLUpdateAccess = CDatabase::GetSingleton ().PrepareStatement (
                  "UPDATE access SET `level`=? WHERE account=? AND channel=?"
                );
            if ( !SQLUpdateAccess )
                return ReportBrokenDB ( &s, 0, "Generando chanserv.SQLUpdateAccess" );
            else
                SQLUpdateAccess->AddRef ( &SQLUpdateAccess );
        }

        // Generamos la consulta SQL para contar el n�mero de registros de un canal
        static CDBStatement* SQLCountAccess = 0;
        if ( !SQLCountAccess )
        {
            SQLCountAccess = CDatabase::GetSingleton ().PrepareStatement (
                  "SELECT COUNT(*) AS count FROM access WHERE channel=?"
                );
            if ( !SQLCountAccess )
                return ReportBrokenDB ( &s, 0, "Generando chanserv.SQLCountAccess" );
            else
                SQLCountAccess->AddRef ( &SQLCountAccess );
        }

        // Obtenemos el nick a a�adir
        CString& szNick = info.GetNextParam ();
        if ( szNick == "" )
            return SendSyntax ( s, "ACCESS ADD" );

        // Obtenemos el nivel
        CString& szLevel = info.GetNextParam ();
        if ( szLevel == "" )
            return SendSyntax ( s, "ACCESS ADD" );
        int iLevel = atoi ( szLevel );

        // Comprobaremos si est� haciendo uso de su nivel de operador
        // para cambiar el acceso.
        bool bOperAccess = false;

        // Comprobamos que tenga acceso a este comando
        if ( ! CheckAccess ( s, ID, LEVEL_ACC_CHANGE ) )
        {
            if ( ! HasAccess ( s, RANK_OPERATOR ) )
                return AccessDenied ( s );
            bOperAccess = true;
        }

        // Comprobamos que la lista de acceso no est� llena
        if ( ! HasAccess ( s, RANK_OPERATOR ) )
        {
            if ( !SQLCountAccess->Execute ( "Q", ID ) )
                return ReportBrokenDB ( &s, SQLCountAccess, "Ejecutando chanserv.SQLCountAccess" );
            unsigned int uiCount;
            if ( SQLCountAccess->Fetch ( 0, 0, "D", &uiCount ) != CDBStatement::FETCH_OK )
            {
                SQLCountAccess->FreeResult ();
                return ReportBrokenDB ( &s, SQLCountAccess, "Obteniendo chanserv.SQLCountAccess" );
            }
            SQLCountAccess->FreeResult ();

            if ( uiCount >= m_options.uiMaxAccessList )
            {
                LangMsg ( s, "ACCESS_ADD_LIST_FULL", szChannel.c_str () );
                return false;
            }
        }
             

        // Comprobamos el nivel
        if ( iLevel < -1 || iLevel == 0 || iLevel > 499 )
        {
            LangMsg ( s, "ACCESS_ADD_INVALID_LEVEL" );
            return false;
        }

        // Obtenemos el id de la cuenta a a�adir
        unsigned long long AccountID = m_pNickserv->GetAccountID ( szNick );
        if ( AccountID == 0ULL )
        {
            LangMsg ( s, "ACCESS_ACCOUNT_NOT_FOUND", szNick.c_str () );
            return false;
        }

        // Obtenemos el nivel del ejecutor
        int iExecutorAccess = GetAccess ( s, ID, true );

        // Comprobamos que la cuenta a a�adir no sea uno mismo, salvo
        // que se trate de un operador o de un fundador.
        if ( AccountID == data.ID && iExecutorAccess < 500 )
        {
            if ( !HasAccess ( s, RANK_OPERATOR ) )
            {
                LangMsg ( s, "ACCESS_ADD_SELF_MODIFICATION" );
                return false;
            }
            bOperAccess = true;
        }

        // Nos aseguramos de que no ponga un nivel superior al suyo.
        if ( iLevel >= iExecutorAccess  )
        {
            if ( !HasAccess ( s, RANK_OPERATOR ) )
                return AccessDenied ( s );
            bOperAccess = true;
        }

        // Obtenemos el nivel actual, para saber si insertamos o actualizamos
        int iCurrentAccess = GetAccess ( AccountID, ID, false, &s );
        bool bUpdated = false;

        // Nos aseguramos de que no intente cambiar el nivel de alguien con tanto o m�s
        // nivel que el suyo.
        if ( iCurrentAccess >= iExecutorAccess )
        {
            if ( !HasAccess ( s, RANK_OPERATOR ) )
               return AccessDenied ( s );
            bOperAccess = true;
        }

        // Obtenemos el nombre de la cuenta
        CString szAccountName;
        m_pNickserv->GetAccountName ( AccountID, szAccountName );

        // Comprobamos si actualizamos o insertamos
        if ( iCurrentAccess == iLevel )
        {
            LangMsg ( s, "ACCESS_ADD_ALREADY_AT_THAT_LEVEL", szAccountName.c_str (), iLevel, szChannel.c_str () );
            return false;
        }
        else if ( iCurrentAccess == 0 )
        {
            if ( !SQLAddAccess->Execute ( "QQd", AccountID, ID, iLevel ) )
                return ReportBrokenDB ( &s, SQLAddAccess, "Ejecutando chanserv.SQLAddAccess" );
            SQLAddAccess->FreeResult ();
        }
        else
        {
            if ( !SQLUpdateAccess->Execute ( "dQQ", iLevel, AccountID, ID ) )
                return ReportBrokenDB ( &s, SQLUpdateAccess, "Ejecutando chanserv.SQLUpdateAccess" );
            SQLUpdateAccess->FreeResult ();
            bUpdated = true;
        }

        // Comprobamos si hay que mandar debug
        CChannel* pChannel = CChannelManager::GetSingleton ().GetChannel ( szChannel );
        if ( pChannel )
        {
            bool bHasDebug = HasChannelDebug ( ID );
            if ( bHasDebug )
            {
                const char* szTopic;
                if ( bUpdated )
                    szTopic = "ACCESS_ADD_MODIFY_DEBUG";
                else
                    szTopic = "ACCESS_ADD_DEBUG";
                LangNotice ( *pChannel, szTopic, s.GetName ().c_str (),
                                                 pChannel->GetName ().c_str (),
                                                 szAccountName.c_str (),
                                                 iLevel );
            }

            // Actualizamos los usuarios a los que afecte
            std::vector < CUser* > vecConnectedUsers;
            if ( m_pNickserv->GetConnectedGroupMembers ( 0, AccountID, vecConnectedUsers ) )
            {
                for ( std::vector < CUser* >::iterator i = vecConnectedUsers.begin ();
                      i != vecConnectedUsers.end ();
                      ++i )
                {
                    CUser* pCur = *i;
                    if ( pChannel->GetMembership ( pCur ) )
                        CheckOnjoinStuff ( *pCur, *pChannel );
                }
            }
        }

        const char* szTopic;
        if ( bUpdated )
            szTopic = "ACCESS_ADD_MODIFY_SUCCESS";
        else
            szTopic = "ACCESS_ADD_SUCCESS";
        LangMsg ( s, szTopic, szAccountName.c_str (), szChannel.c_str (), iLevel );

        // Mandamos un log si se ha modificado el acceso haciendo
        // uso del nivel de operador.
        if ( bOperAccess )
        {
            Log ( "LOG_ACCESS_ADD_OPERATOR", s.GetName ().c_str (), szChannel.c_str (),
                                             szAccountName.c_str (), iLevel );
        }
    }

    else if ( !CPortability::CompareNoCase ( szCommand, "DEL" ) )
    {
        // Generamos la consulta para eliminar accesos
        static CDBStatement* SQLDelAccess = 0;
        if ( !SQLDelAccess )
        {
            SQLDelAccess = CDatabase::GetSingleton ().PrepareStatement (
                  "DELETE FROM access WHERE account=? AND channel=?"
                );
            if ( !SQLDelAccess )
                return ReportBrokenDB ( &s, 0, "Generando chanserv.SQLDelAccess" );
            else
                SQLDelAccess->AddRef ( &SQLDelAccess );
        }

        // Obtenemos el nick a eliminar
        CString& szNick = info.GetNextParam ();
        if ( szNick == "" )
            return SendSyntax ( s, "ACCESS DEL" );

        // Comprobamos que tenga acceso a este comando
        if ( ! HasAccess ( s, RANK_OPERATOR ) && ! CheckAccess ( s, ID, LEVEL_ACC_CHANGE ) )
            return AccessDenied ( s );

        // Obtenemos el id de la cuenta a eliminar
        unsigned long long AccountID = m_pNickserv->GetAccountID ( szNick );
        if ( AccountID == 0ULL )
        {
            LangMsg ( s, "ACCESS_ACCOUNT_NOT_FOUND", szNick.c_str () );
            return false;
        }

        // Obtenemos el nombre de la cuenta
        CString szAccountName;
        m_pNickserv->GetAccountName ( AccountID, szAccountName );

        // Comprobamos que el usuario objetivo tenga acceso
        int iCurrentAccess = GetAccess ( AccountID, ID, false, &s );
        if ( iCurrentAccess == 0 )
        {
            LangMsg ( s, "ACCESS_DEL_NOT_FOUND", szAccountName.c_str (), szChannel.c_str () );
            return false;
        }

        // Comprobamos que el nivel del usuario objetivo no sea mayor
        // o igual que el del ejecutor.
        int iExecutorAccess = GetAccess ( s, ID, true );
        if ( iCurrentAccess >= iExecutorAccess && !HasAccess ( s, RANK_OPERATOR ) )
            return AccessDenied ( s );

        // Eliminamos el registro
        if ( !SQLDelAccess->Execute ( "QQ", AccountID, ID ) )
            return ReportBrokenDB ( &s, SQLDelAccess, "Ejecutando chanserv.SQLDelAccess" );
        SQLDelAccess->FreeResult ();

        // Comprobamos si hay que mandar debug
        CChannel* pChannel = CChannelManager::GetSingleton ().GetChannel ( szChannel );
        if ( pChannel )
        {
            bool bHasDebug = HasChannelDebug ( ID );
            if ( bHasDebug )
            {
                LangNotice ( *pChannel, "ACCESS_DEL_DEBUG", s.GetName ().c_str (),
                                                            szAccountName.c_str (),
                                                            pChannel->GetName ().c_str () );
            }

            // Actualizamos los usuarios a los que afecte
            std::vector < CUser* > vecConnectedUsers;
            if ( m_pNickserv->GetConnectedGroupMembers ( 0, AccountID, vecConnectedUsers ) )
            {
                for ( std::vector < CUser* >::iterator i = vecConnectedUsers.begin ();
                      i != vecConnectedUsers.end ();
                      ++i )
                {
                    CUser* pCur = *i;
                    if ( pChannel->GetMembership ( pCur ) )
                        CheckOnjoinStuff ( *pCur, *pChannel );
                }
            }
        }

        LangMsg ( s, "ACCESS_DEL_SUCCESS", szAccountName.c_str (), szChannel.c_str () );
    }

    else if ( !CPortability::CompareNoCase ( szCommand, "LIST" ) )
    {
        // Generamos la consulta SQL para listar los registros de un canal
        static CDBStatement* SQLListAccess = 0;
        if ( !SQLListAccess )
        {
            SQLListAccess = CDatabase::GetSingleton ().PrepareStatement (
                  "SELECT account.name AS name, access.`level` AS level "
                  "FROM access LEFT JOIN account ON access.account = account.id "
                  "WHERE access.channel = ? ORDER BY level DESC"
                );
            if ( !SQLListAccess )
                return ReportBrokenDB ( &s, 0, "Generando chanserv.SQLListAccess" );
            else
                SQLListAccess->AddRef ( &SQLListAccess );
        }

        // Comprobamos que tenga acceso a este comando
        if ( ! HasAccess ( s, RANK_PREOPERATOR ) && ! CheckAccess ( s, ID, LEVEL_ACC_LIST ) )
            return AccessDenied ( s );

        // Ejecutamos la consulta SQL
        if ( !SQLListAccess->Execute ( "Q", ID ) )
            return ReportBrokenDB ( &s, SQLListAccess, "Ejecutando chanserv.SQLListAccess" );

        // Almacenamos los resultados
        char szNick [ 128 ];
        int iLevel;
        if ( ! SQLListAccess->Store ( 0, 0, "sd", szNick, sizeof ( szNick ), &iLevel ) )
        {
            SQLListAccess->FreeResult ();
            return ReportBrokenDB ( &s, SQLListAccess, "Almacenando chanserv.SQLListAccess" );
        }

        // Hacemos el listado
        LangMsg ( s, "ACCESS_LIST_HEADER", szChannel.c_str () );
        while ( SQLListAccess->FetchStored () == CDBStatement::FETCH_OK )
        {
            char szPadding [ 64 ];
            size_t len = strlen ( szNick );
            const static size_t paddingLen = 30;
            if ( len > paddingLen )
                len = 0;
            else
                len = paddingLen - len;
            memset ( szPadding, ' ', len );
            szPadding [ len ] = '\0';
            LangMsg ( s, "ACCESS_LIST_ENTRY", szNick, szPadding, iLevel );
        }
        SQLListAccess->FreeResult ();
    }

    else
        return SendSyntax ( s, "ACCESS" );

    return true;
}


///////////////////
// OP/DEOP/HALFOP/DEHALFOP/VOICE/DEVOICE
//
bool CChanserv::DoOpdeopEtc ( CUser& s,
                              SCommandInfo& info,
                              const char* szCommand,
                              const char* szPrefix,
                              const char* szFlag,
                              EChannelLevel eRequiredLevel )
{
    // Obtenemos el canal en el que quiere cambiar los modos
    CString& szChannelName = info.GetNextParam ();
    if ( szChannelName == "" )
        return SendSyntax ( s, szCommand );

    // Almacenamos si es operador
    bool bIsOper = HasAccess ( s, RANK_OPERATOR );
    bool bOperMode = false;
    
    // Comprobaremos si debemos hacer debug
    bool bHasDebug = true;

    // Buscamos el canal
    unsigned long long ID;
    CChannel* pChannel = GetRegisteredChannel ( s, szChannelName, ID, bIsOper );
    if ( !pChannel )
        return false;

    // Hacemos una comprobaci�n de acceso
    if ( ID != 0ULL )
    {
        if ( ! CheckAccess ( s, ID, eRequiredLevel ) )
        {
            if ( ! bIsOper )
                return AccessDenied ( s );
            bOperMode = true;
        }

        if ( !bOperMode )
            bHasDebug = HasChannelDebug ( ID );
    }
    else
        bOperMode = true;

    // Buscamos a los usuarios solicitados
    CString szCur = info.GetNextParam ();
    if ( szCur == "" )
        return SendSyntax ( s, szCommand );

    CServer& me = CProtocol::GetSingleton ().GetMe ();
    std::vector < CString > vecModeParams;
    CString szNicks;
    CString szFlags = szPrefix;
    char szNumeric [ 8 ];
    
    do
    {
        // Buscamos al usuario
        CUser* pUser = me.GetUserAnywhere ( szCur );
        if ( pUser )
        {
            pUser->FormatNumeric ( szNumeric );
            szNicks.append ( pUser->GetName () );
            szNicks.append ( " " );
            szFlags.append ( szFlag );
            vecModeParams.push_back ( szNumeric );
        }

        szCur = info.GetNextParam ();
    } while ( szCur != "" );

    // Nos aseguramos de que nos hayan dado al menos un nick v�lido
    if ( vecModeParams.size () == 0 )
        return SendSyntax ( s, szCommand );

    // Eliminamos el espacio superfluo al final
    szNicks.resize ( szNicks.length () - 1 );

    // Enviamos el cambio de modos
    Mode ( pChannel, szFlags, vecModeParams );

    // Enviamos el debug
    if ( bHasDebug )
        LangNotice ( *pChannel, "OPDEOP_ETC_DEBUG", s.GetName ().c_str (), szCommand, szNicks.c_str () );

    // Log
    if ( bOperMode )
        Log ( "LOG_OPDEOP_ETC_OPER", s.GetName ().c_str (), szCommand, pChannel->GetName ().c_str (), szNicks.c_str () );

    return true;
}

///////////////////
// OP
//
COMMAND(Op)
{
    return DoOpdeopEtc ( *( info.pSource ), info, "OP", "+", "o", LEVEL_OPDEOP );
}
///////////////////
// DEOP
//
COMMAND(Deop)
{
    return DoOpdeopEtc ( *( info.pSource ), info, "DEOP", "-", "o", LEVEL_OPDEOP );
}
///////////////////
// HALFOP
//
COMMAND(Halfop)
{
    return DoOpdeopEtc ( *( info.pSource ), info, "HALFOP", "+", "h", LEVEL_HALFOPDEHALFOP );
}
///////////////////
// DEHALFOP
//
COMMAND(Dehalfop)
{
    return DoOpdeopEtc ( *( info.pSource ), info, "DEHALFOP", "-", "h", LEVEL_HALFOPDEHALFOP );
}
///////////////////
// VOICE
//
COMMAND(Voice)
{
    return DoOpdeopEtc ( *( info.pSource ), info, "VOICE", "+", "v", LEVEL_VOICEDEVOICE );
}
///////////////////
// DEVOICE
//
COMMAND(Devoice)
{
    return DoOpdeopEtc ( *( info.pSource ), info, "DEVOICE", "-", "v", LEVEL_VOICEDEVOICE );
}

///////////////////
// OWNER
//
COMMAND(Owner)
{
    CUser& s = *( info.pSource );

    // Obtenemos el canal en el que quiere cambiar los modos
    CString& szChannelName = info.GetNextParam ();
    if ( szChannelName == "" )
        return SendSyntax ( s, "OWNER" );

    // Obtenemos el canal
    unsigned long long ID;
    CChannel* pChannel = GetRegisteredChannel ( s, szChannelName, ID, false );
    if ( !pChannel )
        return false;

    // Obtenemos la membres�a
    CMembership* pMembership = pChannel->GetMembership ( &s );
    if ( !pMembership )
    {
        LangMsg ( s, "OWNER_NOT_IN_CHANNEL", pChannel->GetName ().c_str () );
        return false;
    }

    // Comprobamos si ya es fundador
    if ( pMembership->GetFlags () & CChannel::CFLAG_OWNER )
        return true;

    // Comprobamos si tiene acceso
    if ( GetAccess ( s, ID, true ) != 500 )
        return AccessDenied ( s );

    // Cambiamos los modos
    char szNumeric [ 8 ];
    s.FormatNumeric ( szNumeric );
    BMode ( pChannel, "+q", szNumeric );

    return true;
}

///////////////////
// DEOWNER
//
COMMAND(Deowner)
{
    CUser& s = *( info.pSource );

    // Obtenemos el canal en el que quiere cambiar los modos
    CString& szChannelName = info.GetNextParam ();
    if ( szChannelName == "" )
        return SendSyntax ( s, "DEOWNER" );

    // Obtenemos el canal
    CChannel* pChannel = GetChannel ( s, szChannelName );
    if ( !pChannel )
        return false;

    // Obtenemos la membres�a
    CMembership* pMembership = pChannel->GetMembership ( &s );
    if ( !pMembership )
    {
        LangMsg ( s, "DEOWNER_NOT_OWNER", szChannelName.c_str () );
        return false;
    }

    // Comprobamos si es fundador
    if ( ! ( pMembership->GetFlags () & CChannel::CFLAG_OWNER ) )
    {
        LangMsg ( s, "DEOWNER_NOT_OWNER", szChannelName.c_str () );
        return false;
    }

    // Cambiamos los modos
    char szNumeric [ 8 ];
    s.FormatNumeric ( szNumeric );
    BMode ( pChannel, "-q", szNumeric );

    return true;
}


#undef COMMAND



// Verificaci�n de acceso a los comandos
bool CChanserv::verifyAll ( SCommandInfo& info ) { return true; }
bool CChanserv::verifyPreoperator ( SCommandInfo& info ) { return HasAccess ( *( info.pSource ), RANK_PREOPERATOR ); }
bool CChanserv::verifyOperator ( SCommandInfo& info ) { return HasAccess ( *( info.pSource ), RANK_OPERATOR ); }
bool CChanserv::verifyCoadministrator ( SCommandInfo& info ) { return HasAccess ( *( info.pSource ), RANK_COADMINISTRATOR ); }
bool CChanserv::verifyAdministrator ( SCommandInfo& info ) { return HasAccess ( *( info.pSource ), RANK_ADMINISTRATOR ); }




// Eventos
bool CChanserv::evtJoin ( const IMessage& msg_ )
{
    try
    {
        const CMessageJOIN& msg = dynamic_cast < const CMessageJOIN& > ( msg_ );
        CClient* pSource = msg.GetSource ();

        // Comprobamos que el or�gen del mensaje sea un usuario
        if ( pSource && pSource->GetType () == CClient::USER )
        {
            CUser* pUser = static_cast < CUser* > ( pSource );

            CheckOnjoinStuff ( *pUser, *( msg.GetChannel () ), true );
        }
    }
    catch ( std::bad_cast ) { return false; }

    return true;
}

bool CChanserv::evtMode ( const IMessage& msg_ )
{
    try
    {
        //const CMessageMODE& msg = dynamic_cast < const CMessageMODE& > ( msg_ );
    }
    catch ( std::bad_cast ) { return false; }

    return true;
}

bool CChanserv::evtIdentify ( const IMessage& msg_ )
{
    try
    {
        const CMessageIDENTIFY& msg = dynamic_cast < const CMessageIDENTIFY& > ( msg_ );

        // Comprobamos que exista el usuario
        CUser* pUser = msg.GetUser ();
        if ( pUser )
        {
            // Comprobamos su acceso en todos los canales
            const std::list < CMembership* >& memberships = pUser->GetMemberships ();
            for ( std::list < CMembership* >::const_iterator i = memberships.begin ();
                  i != memberships.end ();
                  ++i )
            {
                const CMembership& membership = *(*i);
                CChannel* pChannel = membership.GetChannel ();

                CheckOnjoinStuff ( *pUser, *pChannel );
            }
        }

    }
    catch ( std::bad_cast ) { return false; }

    return true;
}

bool CChanserv::evtNick ( const IMessage& msg_ )
{
    try
    {
        const CMessageNICK& msg = dynamic_cast < const CMessageNICK& > ( msg_ );
        CClient* pSource = msg.GetSource ();

        if ( pSource && pSource->GetType () == CClient::USER )
        {
            // Si es un cambio de nick, limpiamos la lista de canales en la
            // que est� identificado como fundador.
            CUser* pUser = static_cast < CUser* > ( pSource );
            SServicesData& data = pUser->GetServicesData ();
            data.vecChannelFounder.clear ();
        }
    }
    catch ( std::bad_cast ) { return false; }

    return true;
}

bool CChanserv::evtEOBAck ( const IMessage& msg_ )
{
    try
    {
        const CMessageEOB_ACK& msg = dynamic_cast < const CMessageEOB_ACK& > ( msg_ );

        if ( msg.GetSource () != &( CProtocol::GetSingleton ().GetMe () ) )
        {
            m_bEOBAcked = true;

            // Procesamos ahora las entradas en canales
            for ( std::vector < SJoinProcessQueue >::iterator i = m_vecJoinProcessQueue.begin ();
                  i != m_vecJoinProcessQueue.end ();
                  ++i )
            {
                SJoinProcessQueue& cur = *i;
                CheckOnjoinStuff ( *(cur.pUser), *(cur.pChannel) );
            }
            m_vecJoinProcessQueue.clear ();
        }
    }
    catch ( std::bad_cast ) { return false; }

    return true;
}
