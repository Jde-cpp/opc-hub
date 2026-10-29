import { convertToParamMap } from '@angular/router';
import { HelpTopic, helpTopicFor, helpTopicId, helpTopicRoute, helpTopics } from './help-topic';

describe( 'helpTopicFor', ()=>{
	const topic = ( id:string, routes:string[] ):HelpTopic=>({ id, title: id, url: `assets/x/help/${id}.md`, routes });
	const navigation = topic( 'navigation', [''] );
	const access = topic( 'access', ['access'] );
	const roles = topic( 'roles', ['access/roles'] );
	const gateways = topic( 'gateways', ['gateways', 'gateways/:gateway/:connection', 'apps/gateways/:instance'] );
	const apps = topic( 'apps', ['apps'] );
	const topics = [navigation, access, roles, gateways, apps];

	it( 'matches a literal path', ()=>expect( helpTopicFor(topics, ['access']) ).toBe( access ) );
	it( 'matches as a prefix', ()=>expect( helpTopicFor(topics, ['access', 'users', 'alice']) ).toBe( access ) );
	it( 'prefers the pattern with more literal segments', ()=>expect( helpTopicFor(topics, ['access', 'roles', 'admin']) ).toBe( roles ) );
	it( 'does not let a :param pattern outrank a literal one', ()=>expect( helpTopicFor(topics, ['apps', 'gateways', 'gw1']) ).toBe( gateways ) );//apps/gateways/:instance (2 literals) beats apps (1)
	it( 'accepts any segment for a :param', ()=>expect( helpTopicFor(topics, ['gateways', 'gw1', 'plc']) ).toBe( gateways ) );
	it( 'falls back to the empty pattern', ()=>expect( helpTopicFor(topics, ['nowhere']) ).toBe( navigation ) );
	it( 'returns undefined without a fallback', ()=>expect( helpTopicFor([access, gateways], ['nowhere']) ).toBeUndefined() );
	it( 'breaks a tie by registration order', ()=>{
		const first = topic( 'first', ['apps'] );
		expect( helpTopicFor([first, apps], ['apps']) ).toBe( first );
	});
});

describe( 'helpTopics', ()=>{
	it( 'flattens the multi-provider values in order and tolerates none', ()=>{
		const a:HelpTopic = { id: 'a', title: 'A', url: 'a.md' };
		const b:HelpTopic = { id: 'b', title: 'B', url: 'b.md' };
		expect( helpTopics([[a], [b]]) ).toEqual( [a, b] );
		expect( helpTopics(null) ).toEqual( [] );
	});
});

//a page split out of another's:  its id is the two segments of its url
describe( 'helpTopicRoute and helpTopicId', ()=>{
	const groups:HelpTopic = { id: 'gateways/groups', title: 'Historian groups', url: 'groups.md' };
	it( 'route each segment apart', ()=>expect( helpTopicRoute(groups) ).toEqual( ['/help', 'gateways', 'groups'] ) );
	it( 'read a one-segment url', ()=>expect( helpTopicId(convertToParamMap({topic: 'gateways'})) ).toBe( 'gateways' ) );
	it( 'read a two-segment url', ()=>expect( helpTopicId(convertToParamMap({topic: 'gateways', subtopic: 'groups'})) ).toBe( 'gateways/groups' ) );
} );