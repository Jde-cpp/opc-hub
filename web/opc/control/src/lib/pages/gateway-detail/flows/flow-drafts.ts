import { Service } from '@angular/core';
import { FlowGraph } from '../../../model/flow';

//A graph on its way to a flow's editor, by the flow's guid:  an import's, which makes the flow's row and opens its page with the
//file's graph on the canvas, undeployed, so nothing of it runs until its reader has looked at it and deployed it.
@Service()
export class FlowDrafts{
	#drafts = new Map<string,FlowGraph>();
	put( guid:string, graph:FlowGraph ):void{ this.#drafts.set( guid, graph ); }
	//the draft, which is the editor's from here
	take( guid:string|undefined ):FlowGraph|undefined{
		const y = guid ? this.#drafts.get( guid ) : undefined;
		if( guid )
			this.#drafts.delete( guid );
		return y;
	}
}