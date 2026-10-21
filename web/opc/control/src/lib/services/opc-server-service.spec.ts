import { TestBed } from '@angular/core/testing';
import { provideHttpClient } from '@angular/common/http';
import { AppService, AUTH_STORE, ETransport } from 'jde-framework';
import { OpcServerService } from './opc-server-service';
import { anonymousAuthStore } from 'jde-framework/testing';

const instances = [ {host:'localhost', port:1970, instanceName:'opc1'} ];

//review3 L5: `#servers ??= …` cached whatever the first call produced, INCLUDING a rejection - one dropped request left
//every OpcServer page broken until the browser reloaded.  AccessService.resources already had the .catch reset.
describe( 'OpcServerService.servers', ()=>{
	const configure = ( opcServerInstances:()=>Promise<any[]> )=>{
		TestBed.configureTestingModule({ providers: [
			provideHttpClient(),
			{ provide: AUTH_STORE, useValue: anonymousAuthStore() },
			{ provide: AppService, useValue: {transport: ETransport.Unsecure, opcServerInstances} }
		]});
		return TestBed.inject( OpcServerService );
	};

	it( 'retries after a failed lookup instead of replaying the rejection', async ()=>{
		const lookup = vi.fn().mockRejectedValueOnce( new Error("network") ).mockResolvedValue( instances );
		const service = configure( lookup );

		await expect( service.servers() ).rejects.toThrow( "network" );
		const servers = await service.servers();//the retry the user gets by navigating back to the page
		expect( servers.map(s=>s.name) ).toEqual( ['opc1'] );
		expect( lookup ).toHaveBeenCalledTimes( 2 );
	} );

	it( 'still shares one round trip across concurrent callers', async ()=>{
		const lookup = vi.fn().mockResolvedValue( instances );
		const service = configure( lookup );

		const [a, b] = await Promise.all( [service.servers(), service.servers()] );
		expect( a ).toBe( b );
		expect( lookup ).toHaveBeenCalledTimes( 1 );
	} );

	it( 'reports a stopped server as a miss, not an empty list', async ()=>{
		const service = configure( ()=>Promise.resolve(instances) );
		await expect( service.server('nosuch') ).rejects.toThrow( /No OpcServer instance named 'nosuch'/ );
	} );
} );