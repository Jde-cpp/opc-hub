import { SnackbarService } from 'jde-framework';
import { HistMembers } from './hist-members';

//A19:  the Members-tab plumbing the template and group pages share.
describe( 'HistMembers', ()=>{
	const create = ()=>{
		const snackbar = { exception: vi.fn(), error: vi.fn(), info: vi.fn() };
		return { members: new HistMembers<number>( snackbar as unknown as SnackbarService ), snackbar };
	};

	it( 'lands a read only while the page still shows its record', async ()=>{
		const { members } = create();
		let current = true;
		await members.read( async ()=>[1, 2], rows=>members.rows.set( rows ), ()=>current );
		expect( members.rows() ).toEqual( [1, 2] );
		current = false;
		await members.read( async ()=>[3], rows=>members.rows.set( rows ), ()=>current );
		expect( members.rows() ).toEqual( [1, 2] );
		expect( members.loading() ).toBe( false );
	} );

	it( 'says a refused read on the tab, and a refused edit as a toast', async ()=>{
		const { members, snackbar } = create();
		await members.read( ()=>Promise.reject( new Error( 'no Read' ) ), ()=>{}, ()=>true );
		expect( members.error() ).toBe( 'no Read' );
		expect( members.loading() ).toBe( false );
		let busy:boolean|undefined;
		await members.mutate( async ()=>{ busy = members.busy(); throw new Error( 'no Delete' ); }, 'Could not remove the node.' );
		expect( busy ).toBe( true );
		expect( members.busy() ).toBe( false );
		expect( snackbar.exception ).toHaveBeenCalledWith( 'Could not remove the node.', expect.any( Error ) );
	} );

	it( "says nothing of another record's refusal, and keeps loading while the later read is under way", async ()=>{
		const { members } = create();
		let refuse!:( e:Error )=>void, answer!:( rows:number[] )=>void, record = 'A';
		const a = members.read( ()=>new Promise<number[]>( ( _r, reject )=>refuse = reject ), rows=>members.rows.set( rows ), ()=>record=='A' );
		record = 'B';
		const b = members.read( ()=>new Promise<number[]>( r=>answer = r ), rows=>members.rows.set( rows ), ()=>record=='B' );
		refuse( new Error( "No Read on A's group." ) );
		await a;
		expect( [members.error(), members.loading()] ).toEqual( [undefined, true] );
		answer( [4] );
		await b;
		expect( [members.rows(), members.loading()] ).toEqual( [[4], false] );
		const c = members.read( ()=>new Promise<number[]>( r=>answer = r ), rows=>members.rows.set( rows ), ()=>true );
		await members.read( async ()=>[5], rows=>members.rows.set( rows ), ()=>true );
		answer( [6] );//the earlier of two reads of one record, landing last
		await c;
		expect( members.rows() ).toEqual( [5] );
	} );
} );