import { TestBed } from '@angular/core/testing';
import { AppService } from 'jde-framework';
import { GATEWAY_SERVICE } from './gateway-service';
import { OpcAuthService } from './opc-auth-service';

//reviews/angular-refactor.md B3:  the OPC sign-out went to whichever gateway the url named, falling back to the first - not
//necessarily the one signed into.  It goes to the one the sign-in used.
describe( 'OpcAuthService.logout', ()=>{
	const gateway = ()=>({ login: vi.fn( async ()=>{} ), logout: vi.fn( async ()=>{} ) });
	const create = ()=>{
		const a = gateway(), b = gateway(), app = { login: vi.fn( async ()=>{} ), logout: vi.fn( async ()=>{} ) };
		TestBed.configureTestingModule({ providers: [
			OpcAuthService,
			{ provide: AppService, useValue: app },
			{ provide: GATEWAY_SERVICE, useValue: {gateways: async ()=>[a, b]} }
		]});
		return { auth: TestBed.inject( OpcAuthService ), a, b, app };
	};

	it( 'signs out of the gateway it signed into', async ()=>{
		const { auth, a, b, app } = create();
		await auth.loginPassword( "operator", "secret", undefined );
		await auth.logout();
		expect( a.logout ).toHaveBeenCalledTimes( 1 );
		expect( b.logout ).not.toHaveBeenCalled();
		expect( app.logout ).not.toHaveBeenCalled();
		expect( auth.isOpc() ).toBeUndefined();
	} );

	it( 'signs out of the app server after a Google sign-in', async ()=>{
		const { auth, a, app } = create();
		await auth.login( <any>{} );
		await auth.logout();
		expect( app.logout ).toHaveBeenCalledTimes( 1 );
		expect( a.logout ).not.toHaveBeenCalled();
	} );
} );