import { TestBed } from '@angular/core/testing';
import { vi } from 'vitest';
import { HighchartsLoader } from './highcharts-loader';

//Under jsdom nothing serves the add-on folder - the installed site without it:  the import fails, and the loader says so once
describe( 'HighchartsLoader', ()=>{
	it( 'resolves undefined when the add-on is absent, trying once', async ()=>{
		const info = vi.spyOn( console, 'info' ).mockImplementation( ()=>{} );
		try{
			const loader = TestBed.inject( HighchartsLoader );
			const first = loader.load();
			expect( loader.load() ).toBe( first );
			expect( await first ).toBeUndefined();
			expect( info ).toHaveBeenCalledTimes( 1 );
			expect( info.mock.calls[0][0] ).toContain( '/addons/highcharts/esm/' );
		}
		finally{
			info.mockRestore();
		}
	} );
} );