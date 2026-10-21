import { diffIds } from './slug-row';

describe( 'diffIds', ()=>{
	it( 'says what the edited list added and dropped, each in its own order', ()=>{
		expect( diffIds( [{id: 1}, {id: 2}, {id: 3}], [{id: 4}, {id: 2}, {id: 5}] ) ).toEqual( {added: [4, 5], removed: [1, 3]} );
		expect( diffIds( undefined, [{id: 7}] ) ).toEqual( {added: [7], removed: []} );
	} );
	it( 'takes no edited list for no change, never for an emptied one', ()=>{
		expect( diffIds( [{id: 1}], undefined ) ).toEqual( {added: [], removed: []} );
		expect( diffIds( [{id: 1}], [] ) ).toEqual( {added: [], removed: [1]} );
	} );
} );