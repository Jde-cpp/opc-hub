import { oeeDashboardGroups, OeeLine, OeeLineMember, oeeLineReach, oeeLineTitle, oeeMemberCandidates, oeeMembersOf, OeePlant } from './oee-lines';

//a plant of four pumps:  an area of two lines, the first with a cell in it, a pump in no line, and a reporting line across them
const machines = [1, 2, 3, 4, 5].map( id=>({ id, name: `Pump ${id}`, slug: `pump${id}` }) );
const line = ( id:number, name:string, level:string )=>({ id, name, slug: name.toLowerCase(), level });
const Area = line( 10, 'Packaging', 'Area' ), A = line( 11, 'Line A', 'Line' ), B = line( 12, 'Line B', 'Line' ), Cell = line( 13, 'Cell', 'Cell' ), Site = line( 14, 'Site', 'Site' ), Empty = line( 15, 'Empty', 'Line' );
const member = ( id:number, lineId:number, of:{machine?:number; line?:number}, sort=0 ):OeeLineMember=>({ id, lineId, machineId: of.machine, memberLineId: of.line, sort });
const plant:OeePlant = { machines, lines: [Area, A, B, Cell, Site, Empty], members: [
	member( 1, 10, {line: 12}, 1 ), member( 2, 10, {line: 11}, 0 ),
	member( 3, 11, {machine: 1}, 1 ), member( 4, 11, {line: 13}, 0 ),
	member( 5, 13, {machine: 2} ),
	member( 6, 12, {machine: 3} ), member( 7, 12, {machine: 9} ),//machine 9 is deleted
	member( 8, 99, {machine: 5} )//line 99 is deleted, its members not yet purged
] };
const names = ( list:{name:string}[] )=>list.map( x=>x.name );

describe( "a line's members", ()=>{
	it( 'are in their order, by sort then as they were added', ()=>{
		expect( oeeMembersOf( plant, 10 ).map( m=>m.id ) ).toEqual( [2, 1] );
		expect( oeeMembersOf( plant, 12 ).map( m=>m.id ) ).toEqual( [6, 7] );
	} );
	it( 'reach each machine once, in member order, a member line where it stands, passing over what is deleted', ()=>{
		expect( names( oeeLineReach( plant, 10 ) ) ).toEqual( ['Pump 2', 'Pump 1', 'Pump 3'] );
		expect( names( oeeLineReach( plant, 11 ) ) ).toEqual( ['Pump 2', 'Pump 1'] );
		expect( oeeLineReach( plant, 99 ) ).toEqual( [] );
		const looped = { ...plant, members: [...plant.members, member( 20, 13, {line: 10} )] };//the gateway refuses it;  the walk still ends
		expect( names( oeeLineReach( looped, 10 ) ) ).toEqual( ['Pump 2', 'Pump 1', 'Pump 3'] );
	} );
} );
describe( "the Members picker", ()=>{
	it( "offers the machines the line doesn't reach, through its member lines too", ()=>{
		expect( names( oeeMemberCandidates( plant, 10 ).machines ) ).toEqual( ['Pump 4', 'Pump 5'] );
		expect( names( oeeMemberCandidates( plant, 13 ).machines ) ).toEqual( ['Pump 1', 'Pump 3', 'Pump 4', 'Pump 5'] );
	} );
	it( 'offers the lines of a level no higher than its own', ()=>{
		expect( names( oeeMemberCandidates( plant, 14 ).lines ) ).toEqual( ['Packaging', 'Line A', 'Line B', 'Cell', 'Empty'] );//a site holds any
		expect( names( oeeMemberCandidates( plant, 15 ).lines ) ).toEqual( ['Line A', 'Line B', 'Cell'] );//a line holds lines and cells, no area or site
		expect( names( oeeMemberCandidates( { ...plant, lines: [...plant.lines, line( 16, 'Cell 2', 'Cell' )] }, 16 ).lines ) ).toEqual( ['Cell'] );//a cell holds cells
	} );
	it( 'leaves out itself, its members, a line that reaches it, and one that reaches a machine it reaches', ()=>{
		expect( names( oeeMemberCandidates( plant, 10 ).lines ) ).toEqual( ['Empty'] );//Line A, Line B are its members, the cell's pump it reaches, the site is above it
		expect( names( oeeMemberCandidates( plant, 12 ).lines ) ).toEqual( ['Line A', 'Cell', 'Empty'] );
		expect( names( oeeMemberCandidates( plant, 13 ).lines ) ).toEqual( [] );//Line A reaches it, and it is a cell
		const held = { ...plant, members: [...plant.members, member( 21, 15, {line: 11} )] };//Empty now holds Line A
		expect( names( oeeMemberCandidates( held, 11 ).lines ) ).toEqual( ['Line B'] );//not Empty, which reaches it, nor its own cell
		expect( oeeMemberCandidates( plant, 99 ) ).toEqual( {machines: [], lines: []} );
	} );
} );
describe( "the dashboard's groups", ()=>{
	it( 'are the lines in no line, by level then name, each with the machines it reaches, then the machines in no line', ()=>{
		const { groups, loose } = oeeDashboardGroups( plant );
		expect( groups.map( g=>[g.line.name, names( g.machines )] ) ).toEqual( [['Site', []], ['Packaging', ['Pump 2', 'Pump 1', 'Pump 3']], ['Empty', []]] );
		expect( names( loose ) ).toEqual( ['Pump 4', 'Pump 5'] );//pump 5's line is deleted
	} );
	it( 'has a machine under each line in no line that reaches it', ()=>{
		const reporting = { ...plant, lines: [...plant.lines, line( 17, 'Every pump', 'Line' )], members: [...plant.members, member( 22, 17, {machine: 1} ), member( 23, 17, {machine: 4} )] };
		const { groups, loose } = oeeDashboardGroups( reporting );
		expect( groups.map( g=>[g.line.name, names( g.machines )] ) ).toEqual( [['Site', []], ['Packaging', ['Pump 2', 'Pump 1', 'Pump 3']], ['Empty', []], ['Every pump', ['Pump 1', 'Pump 4']]] );
		expect( names( loose ) ).toEqual( ['Pump 5'] );
	} );
	it( 'is every machine with no lines read', ()=>{
		expect( oeeDashboardGroups( {machines, lines: [], members: plant.members} ) ).toEqual( {groups: [], loose: machines} );
	} );
} );
describe( 'OeeLine', ()=>{
	it( 'creates with its level, and its calendar when it has one', ()=>{
		const row = new OeeLine( {name: 'Packaging', slug: 'packaging', level: 'Area', calendarId: 2} );
		expect( row.canSave ).toBe( true );
		expect( row.mutation( new OeeLine( {} ) ).map( m=>m.toString() ) ).toEqual( ['createOeeLine( slug:"packaging", name:"Packaging", level:"Area", calendarId:2 )'] );
		expect( new OeeLine( {name: 'x', slug: 'x'} ).canSave ).toBe( false );//no level
		expect( new OeeLine( {name: 'x', slug: 'x', level: 'Plant'} ).canSave ).toBe( false );
	} );
	it( 'updates what changed, a calendar cleared as null', ()=>{
		const original = new OeeLine( {id: 10, name: 'Packaging', slug: 'packaging', level: 'Area', calendarId: 2} );
		const edited = new OeeLine( {...original, level: 'Site', calendarId: undefined} );
		expect( edited.equals( original ) ).toBe( false );
		expect( edited.mutation( original ).map( m=>m.toString() ) ).toEqual( ['updateOeeLine( id:10, level:"Site", calendarId:null )'] );
		expect( new OeeLine( original ).mutation( original ) ).toEqual( [] );
	} );
	it( 'is headed by its level and its name', ()=>{
		expect( [oeeLineTitle( Area ), oeeLineTitle( {name: 'Odd'} )] ).toEqual( ['Area Packaging', 'Odd'] );
	} );
} );