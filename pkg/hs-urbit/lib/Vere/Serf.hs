{-# OPTIONS_GHC -Wwarn #-}

{-
    - TODO: `recvLen` is not big-endian safe.
-}

module Vere.Serf ( Serf, SerfState
                 , run, shutdown, kill
                 , replay, bootFromSeq, snapshot
                 , collectFX
                 ) where

import UrbitPrelude hiding (fail)
import Data.Conduit
import Control.Monad.Fail (fail)

import Data.Void
import Noun
import System.Process
import Vere.Pier.Types
import Vere.Ovum
import Vere.FX

import Control.Concurrent     (threadDelay)
import Data.ByteString        (hGet)
import Data.ByteString.Unsafe (unsafeUseAsCString)
import Foreign.Marshal.Alloc  (alloca)
import Foreign.Ptr            (castPtr)
import Foreign.Storable       (peek, poke)
import System.Directory       (createDirectoryIfMissing)
import System.Exit            (ExitCode)

import qualified Data.ByteString.Unsafe as BS
import qualified Data.Text              as T
import qualified Urbit.Time             as Time
import qualified Vere.Log               as Log


-- Types -----------------------------------------------------------------------

data SerfState = SerfState
    { ssNextEv  :: EventId
    , ssLastMug :: Mug
    }
  deriving (Eq, Ord, Show)

data Serf = Serf
  { sendHandle :: Handle
  , recvHandle :: Handle
  , process    :: ProcessHandle
  , sState     :: MVar SerfState
  }

data ShipId = ShipId Ship Bool
  deriving (Eq, Ord, Show)

type Play = Maybe (EventId, Mug, ShipId)

data Plea
    = PPlay Play
    | PWork Work
    | PDone EventId Mug FX
    | PStdr EventId Cord
    | PSlog EventId Word32 Tank
  deriving (Eq, Show)

type GetJobs = EventId -> Word64 -> IO (Vector Job)

type ReplacementEv = Job
type WorkResult    = (SerfState, FX)
type SerfResp      = Either ReplacementEv WorkResult

data SerfExn
    = BadComputeId EventId WorkResult
    | BadReplacementId EventId ReplacementEv
    | UnexpectedPlay EventId Play
    | BadPleaAtom Atom
    | BadPleaNoun Noun [Text] Text
    | ReplacedEventDuringReplay EventId ReplacementEv
    | ReplacedEventDuringBoot   EventId ReplacementEv
    | EffectsDuringBoot         EventId FX
    | SerfConnectionClosed
    | UnexpectedPleaOnNewShip Plea
    | InvalidInitialPlea Plea
  deriving (Show)


-- Instances -------------------------------------------------------------------

instance Exception SerfExn

deriveNoun ''ShipId
deriveNoun ''Plea


-- Utils -----------------------------------------------------------------------

printTank :: Word32 -> Tank -> IO ()
printTank pri = \case
  Leaf (Tape s) -> pure () -- traceM ("[SERF]\t" <> s)
  t             -> pure () -- traceM ("[SERF]\t" <> show (pri, t))

guardExn :: Exception e => Bool -> e -> IO ()
guardExn ok = unless ok . throwIO

fromJustExn :: Exception e => Maybe a -> e -> IO a
fromJustExn Nothing  exn = throwIO exn
fromJustExn (Just x) exn = pure x

fromRightExn :: Exception e => Either a b -> (a -> e) -> IO b
fromRightExn (Left m)  exn = throwIO (exn m)
fromRightExn (Right x) _   = pure x


-- Process Management ----------------------------------------------------------

{-
    TODO `config` is a stub, fill it in.
-}
run :: FilePath -> Acquire Serf
run pierPath = mkAcquire (startUp pierPath) tearDown

startUp :: FilePath -> IO Serf
startUp pierPath = do
    (Just i, Just o, _, p) <- createProcess pSpec
    ss <- newEmptyMVar
    pure (Serf i o p ss)
  where
    chkDir  = traceShowId pierPath
    diskKey = ""
    config  = "0"
    args    = [chkDir, diskKey, config]
    pSpec   = (proc "urbit-worker" args)
                { std_in = CreatePipe
                , std_out = CreatePipe
                }

tearDown :: Serf -> IO ()
tearDown serf = do
  race_ (threadDelay 1000000 >> terminateProcess (process serf))
        (shutdownAndWait serf 0)

waitForExit :: Serf -> IO ExitCode
waitForExit serf = waitForProcess (process serf)

kill :: Serf -> IO ExitCode
kill serf = terminateProcess (process serf) >> waitForExit serf

shutdownAndWait :: Serf -> Word8 -> IO ExitCode
shutdownAndWait serf code = do
  shutdown serf code
  waitForExit serf


-- Basic Send and Receive Operations -------------------------------------------

withWord64AsByteString :: Word64 -> (ByteString -> IO a) -> IO a
withWord64AsByteString w k = do
  alloca $ \wp -> do
    poke wp w
    bs <- BS.unsafePackCStringLen (castPtr wp, 8)
    k bs

sendLen :: Serf -> Int -> IO ()
sendLen s i = do
  w <- evaluate (fromIntegral i :: Word64)
  withWord64AsByteString (fromIntegral i) (hPut (sendHandle s))

sendOrder :: Serf -> Order -> IO ()
sendOrder w o = do
  traceM ("[DEBUG] Serf.sendOrder.toNoun: " <> show o)
  n <- evaluate (toNoun o)
  traceM ("[DEBUG] Serf.sendOrder.jam")
  j <- evaluate (jam n)
  traceM ("[DEBUG] Serf.sendOrder.send")
  sendAtom w j

sendAtom :: Serf -> Atom -> IO ()
sendAtom s a = do
    let bs = unpackAtom a
    sendLen s (length bs)
    hPut (sendHandle s) bs
    hFlush (sendHandle s)
  where
    unpackAtom :: Atom -> ByteString
    unpackAtom = view atomBytes

recvLen :: Serf -> IO Word64
recvLen w = do
  bs <- hGet (recvHandle w) 8
  case length bs of
    8 -> unsafeUseAsCString bs (peek . castPtr)
    _ -> throwIO SerfConnectionClosed

recvBytes :: Serf -> Word64 -> IO ByteString
recvBytes w = do
  hGet (recvHandle w) . fromIntegral

recvAtom :: Serf -> IO Atom
recvAtom w = do
    len <- recvLen w
    bs <- recvBytes w len
    pure (packAtom bs)
  where
    packAtom :: ByteString -> Atom
    packAtom = view (from atomBytes)

cordString :: Cord -> String
cordString (Cord bs) = unpack $ T.strip $ decodeUtf8 bs


--------------------------------------------------------------------------------

snapshot :: Serf -> SerfState -> IO ()
snapshot serf SerfState{..} = sendOrder serf (OSave $ ssNextEv - 1)

shutdown :: Serf -> Word8 -> IO ()
shutdown serf code = sendOrder serf (OExit code)

{-
    TODO Find a cleaner way to handle `PStdr` Pleas.
-}
recvPlea :: Serf -> IO Plea
recvPlea w = do
  a <- recvAtom w
  n <- fromRightExn (cue a) (const $ BadPleaAtom a)
  p <- fromRightExn (fromNounErr n) (\(p,m) -> BadPleaNoun (traceShowId n) p m)

  case p of PStdr e msg   -> do -- traceM ("[SERF]\t" <> (cordString msg))
                                recvPlea w
            PSlog _ pri t -> do printTank pri t
                                recvPlea w
            _             -> do traceM ("[DEBUG] Serf.recvPlea: Got " <> show p)
                                pure p

{-
    Waits for initial plea, and then sends boot IPC if necessary.
-}
handshake :: Serf -> LogIdentity -> IO SerfState
handshake serf ident = do
    ss@SerfState{..} <- recvPlea serf >>= \case
      PPlay Nothing          -> pure $ SerfState 1 (Mug 0)
      PPlay (Just (e, m, _)) -> pure $ SerfState e m
      x                      -> throwIO (InvalidInitialPlea x)

    when (ssNextEv == 1) $ do
        sendOrder serf (OBoot ident)

    pure ss

sendWork :: Serf -> Job -> IO SerfResp
sendWork w job =
  do
    sendOrder w (OWork job)
    res <- loop
    pure res
  where
    eId = jobId job

    produce :: WorkResult -> IO SerfResp
    produce (ss@SerfState{..}, o) = do
      guardExn (ssNextEv == (1+eId)) (BadComputeId eId (ss, o))
      pure $ Right (ss, o)

    replace :: ReplacementEv -> IO SerfResp
    replace job = do
      guardExn (jobId job == eId) (BadReplacementId eId job)
      pure (Left job)

    loop :: IO SerfResp
    loop = recvPlea w >>= \case
      PPlay p       -> throwIO (UnexpectedPlay eId p)
      PDone i m o   -> produce (SerfState (i+1) m, o)
      PWork work    -> replace (DoWork work)
      PStdr _ cord  -> loop -- traceM ("[SERF]\t" <> cordString cord) >> loop
      PSlog _ pri t -> printTank pri t >> loop


--------------------------------------------------------------------------------

doJob :: Serf -> Job -> IO (Job, SerfState, FX)
doJob serf job = do
    sendWork serf job >>= \case
        Left replaced  -> doJob serf replaced
        Right (ss, fx) -> pure (job, ss, fx)

bootJob :: Serf -> Job -> IO (Job, SerfState)
bootJob serf job = do
    doJob serf job >>= \case
        (job, ss, []) -> pure (job, ss)
        (job, ss, fx) -> throwIO (EffectsDuringBoot (jobId job) fx)

replayJob :: Serf -> Job -> IO SerfState
replayJob serf job = do
    sendWork serf job >>= \case
        Left replace  -> throwIO (ReplacedEventDuringReplay (jobId job) replace)
        Right (ss, _) -> pure ss


--------------------------------------------------------------------------------

type BootSeqFn = EventId -> Mug -> Time.Wen -> Job

data BootExn = ShipAlreadyBooted
  deriving stock    (Eq, Ord, Show)
  deriving anyclass (Exception)

bootFromSeq :: Serf -> BootSeq -> IO ([Job], SerfState)
bootFromSeq serf (BootSeq ident nocks ovums) = do
    handshake serf ident >>= \case
        ss@(SerfState 1 (Mug 0)) -> loop [] ss bootSeqFns
        _                        -> throwIO ShipAlreadyBooted

  where
    loop :: [Job] -> SerfState -> [BootSeqFn] -> IO ([Job], SerfState)
    loop acc ss = \case
        []   -> pure (reverse acc, ss)
        x:xs -> do wen       <- Time.now
                   job       <- pure $ x (ssNextEv ss) (ssLastMug ss) wen
                   (job, ss) <- bootJob serf job
                   loop (job:acc) ss xs

    bootSeqFns :: [BootSeqFn]
    bootSeqFns = fmap muckNock nocks <> fmap muckOvum ovums
      where
        muckNock nok eId mug _   = RunNok $ LifeCyc eId mug nok
        muckOvum ov  eId mug wen = DoWork $ Work eId mug wen ov
{-
    The ship is booted, but it is behind. shove events to the worker
    until it is caught up.
-}
replayJobs :: Serf -> SerfState -> ConduitT Job Void IO SerfState
replayJobs serf = go
  where
    go ss = await >>= maybe (pure ss) (liftIO . replayJob serf >=> go)

replay :: Serf -> Log.EventLog -> IO SerfState
replay serf log = do
    ss <- handshake serf (Log.identity log)

    runConduit $  Log.streamEvents log (ssNextEv ss)
               .| toJobs (Log.identity log) (ssNextEv ss)
               .| replayJobs serf ss

toJobs :: LogIdentity -> EventId -> ConduitT ByteString Job IO ()
toJobs ident eId =
    await >>= \case
        Nothing -> traceM "no more jobs" >> pure ()
        Just at -> do yield =<< liftIO (fromAtom at)
                      traceM (show eId)
                      toJobs ident (eId+1)
  where
    isNock = trace (show (eId, lifecycleLen ident))
           $ eId <= fromIntegral (lifecycleLen ident)

    fromAtom :: ByteString -> IO Job
    fromAtom bs | isNock = do
        noun       <- cueBSExn bs
        (mug, nok) <- fromNounExn noun
        pure $ RunNok (LifeCyc eId mug nok)
    fromAtom bs = do
        noun            <- cueBSExn bs
        (mug, wen, ovm) <- fromNounExn noun
        pure $ DoWork (Work eId mug wen ovm)


-- Collect Effects for Parsing -------------------------------------------------

collectFX :: Serf -> Log.EventLog -> IO ()
collectFX serf log = do
    ss <- handshake serf (Log.identity log)

    let pax = "/home/benjamin/testnet-zod-fx"

    createDirectoryIfMissing True pax

    runConduit $  Log.streamEvents log (ssNextEv ss)
               .| toJobs (Log.identity log) (ssNextEv ss)
               .| doCollectFX serf ss
               .| persistFX pax

persistFX :: FilePath -> ConduitT (EventId, FX) Void IO ()
persistFX pax = await >>= \case
    Nothing        -> pure ()
    Just (eId, fx) -> do
        writeFile (pax <> "/" <> show eId) (jamBS $ toNoun fx)
        persistFX pax

doCollectFX :: Serf -> SerfState -> ConduitT Job (EventId, FX) IO ()
doCollectFX serf = go
  where
    go :: SerfState -> ConduitT Job (EventId, FX) IO ()
    go ss = await >>= \case
        Nothing -> pure ()
        Just jb -> do
            (_, ss, fx) <- liftIO (doJob serf jb)
            liftIO $ print (jobId jb)
            yield (jobId jb, fx)
            go ss

