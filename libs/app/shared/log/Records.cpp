#include <jde/app/log/Records.h>
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
		const DailyFile fresh;
		let& known = restart ? fresh : *this;
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
		std::ranges::copy( Salvaged, std::back_inserter(run) );
		std::ranges::move( records, std::back_inserter(run) );
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
		}
		Committed( run.End, run.Chain, move(run.Runs) );
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
		Opened = true;
	}

	//The scan's decoder:  each frame's time, and the whole records after the last checkpoint, which nothing proves and
	//the next append writes again.
	struct Opening::Decoder final : IO::Records::IDecoder{
		α Decode( const Raw& raw )ι->std::expected<optional<IO::Records::Time>,EStop> override{
			FileEntry r;
			if( let stop = IO::Records::Parse(raw, r); stop )
				return std::unexpected{ *stop };
			let t = Log::Time( r );
			if( raw.Field!=StartField && raw.Field!=CheckpointField )
				_pending.push_back( move(r) );
			return t;
		}
		α Chain()Ι->IO::Records::State override{ return 0; }
		α Sealed()ι->void override{ _pending.clear(); }
		α Dropped()ι->void override{ Torn = move( _pending ); _pending.clear(); }
		α Consumes()Ι->bool override{ return false; }
		vector<FileEntry> Torn;
	private:
		vector<FileEntry> _pending;
	};

	Opening::Opening( fs::path path, SL sl )ε:
		_y{ {.Path=move(path)} }{
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
		_decoder = mu<Decoder>();
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
		else
			_y.Salvaged = move( _decoder->Torn );
		if( scanned.Size<scanned.FileSize )
			_y.Unopened = move( scanned );
		return ENext::End;
	}

	α DailyOpenAwait::Open()ι->VoidTask{
		try{
			Opening opening{ _path, Source() };
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

	α RunsReadAwait::Read()ι->VoidTask{
		try{
			vector<FileEntry> y;
			for( let& run : _runs ){
				Reader reader{ run };
				FileEntry r;
				for( auto next = reader.Next(r); next!=ENext::End; next = reader.Next(r) ){
					if( next==ENext::More ){
						let w = *reader.Wants();
						IO::ReadAwait read{ _file, IO::ReadOptions{.Offset=w.Offset, .Size=w.Size, .Tags=_tags} };
						reader.Feed( co_await Any(read) );
					}
					else if( reader.Field()!=StartField && reader.Field()!=CheckpointField )
						y.push_back( move(r) );
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