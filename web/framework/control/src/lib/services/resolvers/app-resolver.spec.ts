import { Component } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { provideRouter } from '@angular/router';
import { RouteStore } from 'jde-spa';
import { APP_SERVICE } from '../app/app-service';
import { AppResolver, Connection } from './app-resolver';

@Component( {template: ''} )
class Blank {}

//the /apps rows as the AppServer lists them, in connection order
const rows = ()=>[
	{ id: 1, programName: 'Jde.OpcServer', instanceName: 'OpcServer.mysql.debug', hostName: 'localhost', created: 0 },
	{ id: 2, programName: 'Jde.Opc.PlcEmulator', instanceName: 'Debug', hostName: 'workstation25', created: 0 },
	{ id: 3, programName: 'Jde.OpcHub', instanceName: 'OpcHub.debug', hostName: 'workstation25', created: 0 },
	{ id: 4, programName: 'Jde.AppServer', instanceName: 'AppServer.debug', hostName: 'workstation25', created: 0 }
];

describe( 'AppResolver', ()=>{
	async function resolve():Promise<Connection[]>{
		TestBed.configureTestingModule( {providers: [
			provideRouter( [
				{ path: 'apps', component: Blank },
				{ path: 'apps/gateways/:instance', component: Blank },
				{ path: 'apps/appServers/:instance', component: Blank },
				{ path: 'apps/opcServers/:instance', component: Blank }
			] ),
			AppResolver,
			{provide: APP_SERVICE, useValue: {queryArray: async ()=>rows()}},
			{provide: RouteStore, useValue: {setChildren: ()=>{}}}
		]} );
		return TestBed.inject( AppResolver ).resolve( null!, null! );
	}

	it( 'labels the gateway and the PLC emulator, and leaves the hub its own name', async ()=>{
		const byInstance = Object.fromEntries( (await resolve()).map(c=>[c.instanceName, c.displayName]) );
		expect( byInstance ).toEqual( {'OpcHub.debug': 'OpcHub', 'AppServer.debug': 'AppServer', 'OpcServer.mysql.debug': 'OpcServer', 'Debug': 'PLC Emulator'} );
	});

	it( 'puts the hub first, then orders by name', async ()=>{
		expect( (await resolve()).map(c=>c.displayName) ).toEqual( ['OpcHub', 'AppServer', 'OpcServer', 'PLC Emulator'] );
	});

	it( 'links only the services the site has a page for', async ()=>{
		const linked = Object.fromEntries( (await resolve()).map(c=>[c.displayName, c.linked]) );
		expect( linked ).toEqual( {OpcHub: true, AppServer: true, OpcServer: true, 'PLC Emulator': false} );
	});
});
