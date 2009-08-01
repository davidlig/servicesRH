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
// Archivo:     COperserv.h
// Prop�sito:   Servicio para operadores
// Autores:     Alberto Alonso <rydencillo@gmail.com>
//

#pragma once

class COperserv : public CService
{
public:
                    COperserv       ( const CConfig& config );
    virtual         ~COperserv      ( );

    void            Load            ( );
    void            Unload          ( );

    // Comandos
protected:
    void            UnknownCommand  ( SCommandInfo& info );
private:
#define COMMAND(x) bool cmd ## x ( SCommandInfo& info )
#define SET_COMMAND(x) bool cmd ## x ( SCommandInfo& info, unsigned long long IDTarget )
    COMMAND(Help);
    COMMAND(Raw);
    COMMAND(Load);
    COMMAND(Unload);
    COMMAND(Table);
#undef SET_COMMAND
#undef COMMAND

    // Verificaci�n de acceso a comandos
private:
    bool            verifyPreoperator       ( SCommandInfo& info );
    bool            verifyOperator          ( SCommandInfo& info );
    bool            verifyCoadministrator   ( SCommandInfo& info );
    bool            verifyAdministrator     ( SCommandInfo& info );

private:
};
