#include <jde/app/log/Records.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/fwk/io/records/Scan.h>

#define let const auto

namespace Jde::App{
	constexpr ELogTags _tags{ ELogTags::ExternalLogger };
	using IO::Records::Raw;

	α Log::Time( const FileEntry& r )ι->optional<int64_t>{
		switch( r.value_case() ){
		case FileEntry::kEntry: return Ns( r.entry().time() );
		default: return nullopt;
		}
	}

	α Log::HeadOf( sv bytes )ι->optional<ArchiveHead>{
		IO::Records::Cursor cursor{ bytes, 0 };
		let raw = cursor.Next();
		if( !raw || raw->Field!=StartField )
			return nullopt;
		let start = IO::Records::DecodeStart( raw->Body );
		if( !start )
			return nullopt;
		ArchiveHead y{ *start };
		if( let next = cursor.Next(); next && next->Field==FileEntry::kMerged ){
			Proto::Merged merged;
			if( merged.ParseFromArray(next->Body.data(), (int)next->Body.size()) )
				y.Merged = move( merged );
		}
		return y;
	}

	α Log::ParseRecords( sv bytes, const fs::path& path, SL sl )ε->vector<FileEntry>{
		Reader reader{ Run{.Offset=0, .End=(uint)bytes.size(), .Chain=0, .First=0, .Last=0} };
		vector<FileEntry> y;
		FileEntry r;
		for( auto next = reader.Next(r); next!=ENext::End; next = reader.Next(r) ){
			if( next==ENext::More ){
				let w = *reader.Wants();
				reader.Feed( string{bytes.substr(w.Offset, w.Size)} );
			}
			else if( reader.Field()!=StartField && reader.Field()!=CheckpointField )
				y.push_back( move(r) );
		}
		if( let stop = *reader.Stop(); stop!=EStop::End )
			throw IO::IOException{ sl, path, ELogLevel::Error, "reads {} at byte {}, so it can't be read through", IO::Records::ToString(stop), reader.Offset() };
		return y;
	}
}
namespace Jde::App::Log{
	using Protobuf::ToGuid;
	α FileStrings::Holds( uuid id )ι->bool{
		let p = _ids.find( id );
		if( p==_ids.end() )
			return false;
		p->second = ++_sequence;
		return true;
	}
	α FileStrings::Add( uuid id )ι->void{
		_ids.insert_or_assign( id, ++_sequence );
		if( _ids.size()<=Cap )
			return;
		vector<uint> uses;//the newer half stays.
		uses.reserve( _ids.size() );
		for( let& [_,use] : _ids )
			uses.push_back( use );
		let middle = uses.begin()+uses.size()/2;
		std::ranges::nth_element( uses, middle );
		absl::erase_if( _ids, [oldest=*middle]( let& p ){ return p.second<oldest; } );
	}

	α Pending::Write( SL sl )ι->IO::WriteAwait{
		return IO::WriteAwait{ Path, move(Bytes), IO::WriteOptions{.Create=true, .Mode=IO::EWriteMode::Truncate, .Offset=Offset, .Tags=_tags}, sl };
	}

	α DailyFile::Prepare( vector<FileEntry> records, SL sl )ε->Pending{
		std::error_code ec;
		bool restart{};
		if( !Refused.empty() ){
			if( fs::exists(Path, ec) || ec )
				throw IO::IOException{ sl, Path, Discarded++ ? ELogLevel::Debug : ELogLevel::Error, "{}, so the log won't write to it until it is repaired or removed", Refused };
			restart = true;//removed since.
		}
		if( Unopened && !restart ){
			try{
				IO::Records::Truncate( Path, *Unopened, sl );
			}
			catch( ... ){//no longer the file that was scanned, perhaps, so the next write scans it again.
				Opened = false;
				throw;
			}
			Unopened.reset();
		}
		try{
			restart = IO::Records::Verify( *this, false, restart, sl ).Restart;
		}
		catch( ... ){//other bytes than the log wrote, so its next write scans it again.
			Opened = false;
			throw;
		}
		DailyFile fresh;
		auto& known = restart ? fresh : *this;
		Pending y{ .Path=Path, .Offset=known.Size, .Restart=restart, .Identity=known.Identity };
		optional<IO::Records::Opening<FileEntry>> opening;
		if( !known.Size ){
			IO::Proto::FileStart first;
			first.set_ts( Ns(Clock::now()) );
			opening.emplace( first, vector<FileEntry>{}, vector<FileEntry>{} );
			y.Identity = first.ts();
		}
		vector<FileEntry> run;
		run.reserve( Salvaged.size()+records.size() );
		auto add = [&]( FileEntry&& r ){
			if( r.has_str() ){
				let id = ToGuid( r.str().id() );
				if( known.Strings.Holds(id) )
					return;
				y.Strings.push_back( id );
			}
			run.push_back( move(r) );
		};
		for( auto r : Salvaged )
			add( move(r) );
		for( auto& r : records )
			add( move(r) );
		auto built = IO::Records::BuildAppend<FileEntry>( known.Size, known.Chain, move(opening), move(run), {} );
		y.End = built.End;
		y.Chain = built.Chain;
		y.Runs = move( built.Runs );
		y.Bytes = move( built.Bytes );
		Hand( y.Offset, y.End );
		return y;
	}
	α DailyFile::Commit( Pending&& run )ι->void{
		if( run.Restart ){
			Runs.clear();
			Unopened.reset();
			Outstanding.reset();
			Refused.clear();
			Discarded = 0;
			Strings.Clear();
		}
		Committed( run.End, run.Chain, move(run.Runs) );
		for( let& id : run.Strings )
			Strings.Add( id );
		Identity = run.Identity;
		Salvaged.clear();
	}
	α DailyFile::Removed()ι->void{
		Size = 0;
		Chain = 0;
		Runs.clear();
		Unopened.reset();
		Outstanding.reset();
		Named = false;
		Refused.clear();
		Discarded = 0;
		Identity = 0;
		Strings.Clear();
		Opened = true;
	}

	α DailyFile::Current()Ι->bool{
		if( !Opened || Refused.empty() )
			return Opened;
		let now = IO::Records::StampOf( Path );
		return !now || now==Stamped;//one removed is Prepare's restart.
	}

	//The scan's decoder:  each frame's time, the Strings a checkpoint proves, and the whole records after the last
	//checkpoint, which nothing proves and the next append writes again.
	struct Opening::Decoder final : IO::Records::IDecoder{
		Decoder( FileStrings& strings )ι:_strings{ strings }{}
		α Decode( const Raw& raw )ι->std::expected<optional<IO::Records::Time>,EStop> override{
			FileEntry r;
			if( let stop = IO::Records::Parse(raw, r); stop )
				return std::unexpected{ *stop };
			let t = Log::Time( r );
			if( r.has_str() )
				_ids.push_back( ToGuid(r.str().id()) );
			if( raw.Field!=StartField && raw.Field!=CheckpointField )
				_pending.push_back( move(r) );
			return t;
		}
		α Chain()Ι->IO::Records::State override{ return 0; }
		α Sealed()ι->void override{
			for( let& id : _ids )
				_strings.Add( id );
			_ids.clear();
			_pending.clear();
		}
		α Dropped()ι->void override{ Torn = move( _pending ); _pending.clear(); _ids.clear(); }
		α Consumes()Ι->bool override{ return false; }
		vector<FileEntry> Torn;
	private:
		FileStrings& _strings;
		vector<uuid> _ids;
		vector<FileEntry> _pending;
	};

	Opening::Opening( fs::path path, const DailyFile& was, SL sl )ε:
		_y{ {.Path=move(path)} },
		_outstanding{ was.Outstanding },
		_salvaged{ was.Salvaged }{
		_y.Opened = true;
		try{
			_file = ms<IO::OpenFile>( _y.Path, sl );
		}
		catch( IO::IOException& e ){
			if( e.Error!=IO::EIOError::NotFound ){
				e.SetLevel( ELogLevel::Error );
				throw;
			}
			return;
		}
		_y.Stamped = IO::Records::StampOf( _y.Path );//before the scan, so a change during it shows next time.
		_decoder = mu<Decoder>( _y.Strings );
		_scanner.emplace( _file, StartField, CheckpointField, *_decoder, sl );
	}
	Opening::~Opening()=default;
	α Opening::Wants()Ι->optional<Window>{ return _scanner ? _scanner->Wants() : nullopt; }
	α Opening::Feed( string&& bytes )ι->void{
		if( _scanner )
			_scanner->Feed( move(bytes) );
	}
	α Opening::Next()ε->ENext{
		if( !_scanner )
			return ENext::End;
		if( _scanner->Next()==ENext::More )
			return ENext::More;
		auto scanned = _scanner->Result();
		_scanner.reset();
		_y.Size = scanned.Size;
		_y.Chain = scanned.Chain;
		_y.Runs = move( scanned.Runs );
		_y.Identity = scanned.Start ? scanned.Start->ts() : 0;
		if( scanned.Keep() )
			_y.Refused = "holds damage a truncation would lose, or is another program's";
		else if( scanned.Start && scanned.Start->generation() )
			_y.Refused = "is an archive";
		else if( _outstanding && scanned.Size>=_outstanding->Offset && scanned.FileSize<=_outstanding->End )
			_y.Salvaged = move( _salvaged );
		else
			_y.Salvaged = move( _decoder->Torn );
		if( scanned.Size<scanned.FileSize )
			_y.Unopened = move( scanned );
		return ENext::End;
	}

	α DailyOpenAwait::Open()ι->VoidTask{
		try{
			Opening opening{ _path, _was, Source() };
			while( opening.Next()==ENext::More ){
				let w = *opening.Wants();
				IO::ReadAwait read{ opening.File(), IO::ReadOptions{.Offset=w.Offset, .Size=w.Size, .Tags=_tags} };
				opening.Feed( co_await Any(read) );
			}
			Resume( opening.Take() );
		}
		catch( Exception& e ){
			ResumeExp( move(e) );
		}
		catch( std::runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	//Runs that follow each other, as a live file's appends do, joined into one span:  a read of many is one read.
	Ω spans( const vector<Run>& runs )ι->vector<Run>{
		vector<Run> y;
		for( let& run : runs ){
			if( y.size() && y.back().End==run.Offset ){
				y.back().End = run.End;
				y.back().First = std::min( y.back().First, run.First );
				y.back().Last = std::max( y.back().Last, run.Last );
			}
			else
				y.push_back( run );
		}
		return y;
	}
	α RunsReadAwait::Read()ι->VoidTask{
		try{
			vector<FileEntry> y;
			for( let& span : spans(_runs) ){
				Reader reader{ span, IO::Records::WindowBytes };
				FileEntry r;
				for( auto next = reader.Next(r); next!=ENext::End; next = reader.Next(r) ){
					if( next==ENext::More ){
						let w = *reader.Wants();
						IO::ReadAwait read{ _file, IO::ReadOptions{.Offset=w.Offset, .Size=w.Size, .Tags=_tags} };
						reader.Feed( co_await Any(read) );
					}
					else if( reader.Field()!=StartField && reader.Field()!=CheckpointField ){
						y.push_back( move(r) );
						if( _ends )
							_ends->push_back( reader.Offset() );
					}
				}
				if( let stop = *reader.Stop(); stop!=EStop::End )
					throw IO::IOException{ Source(), _file->Path(), ELogLevel::Error, "reads {} at byte {}, in what its scan kept, so it can't be read through", IO::Records::ToString(stop), reader.Offset() };
			}
			Resume( move(y) );
		}
		catch( Exception& e ){
			ResumeExp( move(e) );
		}
		catch( std::runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}