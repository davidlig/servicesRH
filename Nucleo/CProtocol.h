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
// Archivo:     CProtocol.h
// Prop�sito:   Protocolo de cliente
// Autores:     Alberto Alonso <rydencillo@gmail.com>
//

#pragma once

#define PROTOCOL_CALLBACK CCallback < bool, const IMessage& >

class CProtocol
{
private:
    struct SCommandCallbacks
    {
        IMessage* pMessage;
        std::vector < PROTOCOL_CALLBACK* > vecCallbacks;
    };
    enum EHandlerStage
    {
        HANDLER_BEFORE_CALLBACKS        = 0x0001,
        HANDLER_IN_CALLBACKS            = 0x0002,
        HANDLER_AFTER_CALLBACKS         = 0x0004,
        HANDLER_ALL_CALLBACKS           = HANDLER_BEFORE_CALLBACKS | HANDLER_IN_CALLBACKS | HANDLER_AFTER_CALLBACKS
    };
    typedef google::dense_hash_map < const char*, SCommandCallbacks, SStringHasher, SStringEquals > t_commandsMap;

private:
    static CProtocol*       ms_pInstance;

public:
    static CProtocol&       GetSingleton        ( );
    static CProtocol*       GetSingletonPtr     ( );

private:
                            CProtocol           ( );
public:
    virtual                 ~CProtocol          ( );

    virtual bool            Initialize          ( const CSocket& socket, const CConfig& config );
    virtual int             Loop                ( );
    virtual bool            Process             ( const CString& szLine );
    virtual int             Send                ( const IMessage& message, CClient* pSource = NULL );

    CSocket&                GetSocket           () { return m_socket; }

    inline CServer&         GetMe               ( ) { return m_me; }
    inline const CServer&   GetMe               ( ) const { return m_me; }

    void                    DelayedDelete       ( CDelayedDeletionElement* pElement );
private:
    void                    DeleteDelayedElements ( );
public:

    void                    AddHandler          ( const IMessage& message, const PROTOCOL_CALLBACK& callback );
    void                    RemoveHandler       ( const IMessage& message, const PROTOCOL_CALLBACK& callback );

    // Distributed database
    unsigned int            GetDDBVersion       ( ) const { return m_uiDDBVersion; }
    unsigned int            GetDDBTableSerial   ( unsigned char ucTable ) const { return m_uiDDBSerials [ ucTable ]; }
    void                    InsertIntoDDB       ( unsigned char ucTable,
                                                  const CString& szKey,
                                                  const CString& szValue,
                                                  const CString& szTarget = "*" );
    const char*             GetDDBValue         ( unsigned char ucTable, const CString& szKey ) const;


    void                    ConvertToLowercase  ( CString& szString ) const;
    char*                   HashIP              ( char* dest, const char* szHost,
                                                  unsigned int uiAddress, const char* szKey ) const;

    CString                 GetUserVisibleHost  ( CUser& user ) const;

private:
    void                    InternalAddHandler      ( unsigned long ulStage,
                                                      const IMessage& message,
                                                      const PROTOCOL_CALLBACK& callback );
    void                    InternalAddHandler      ( t_commandsMap& map,
                                                      const IMessage& message,
                                                      const PROTOCOL_CALLBACK& callback );

    void                    TriggerMessageHandlers  ( unsigned long ulStage,
                                                      const IMessage& message );

private:
    // Eventos
    bool                    evtEndOfBurst       ( const IMessage& message );
    bool                    evtPing             ( const IMessage& message );
    bool                    evtServer           ( const IMessage& message );
    bool                    evtSquit            ( const IMessage& message );
    bool                    evtNick             ( const IMessage& message );
    bool                    evtQuit             ( const IMessage& message );
    bool                    evtKill             ( const IMessage& message );
    bool                    evtMode             ( const IMessage& message );
    bool                    evtBmode            ( const IMessage& message );
    bool                    evtBurst            ( const IMessage& message );
    bool                    evtTburst           ( const IMessage& message );
    bool                    evtTopic            ( const IMessage& message );
    bool                    evtCreate           ( const IMessage& message );
    bool                    evtJoin             ( const IMessage& message );
    bool                    evtPart             ( const IMessage& message );
    bool                    evtKick             ( const IMessage& message );
    bool                    evtDB               ( const IMessage& message );
    bool                    evtRaw              ( const IMessage& message );
    bool                    evtAway             ( const IMessage& message );
    bool                    evtWhois            ( const IMessage& message );

private:
    CSocket                 m_socket;
    CConfig                 m_config;
    CString                 m_szLine;
    CServer                 m_me;
    t_commandsMap           m_commandsMap;
    t_commandsMap           m_commandsMapBefore;
    t_commandsMap           m_commandsMapAfter;
    bool                    m_bGotServer;
    CString                 m_szHiddenAddress;
    CString                 m_szHiddenDesc;
    std::vector < CDelayedDeletionElement* >
                            m_vecDelayedDeletionElements;

    // Distributed database
    typedef google::dense_hash_map < char*, char*, SStringHasher, SStringEquals > t_mapDDB;
    bool                    m_bDDBInitialized;
    unsigned int            m_uiDDBVersion;
    unsigned int            m_uiDDBSerials [ 256 ];
    t_mapDDB                m_mapDDB [ 256 ];
};
