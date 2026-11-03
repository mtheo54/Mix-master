# OMNIS — carte du code

> Générée par `python3 tools/codemap/codemap.py` (ne pas éditer à la main). Une ligne par module (X.h + X.cpp) : rôle, types, fonctions, tests. Chercher ici (grep) avant d'ouvrir des sources, puis n'ouvrir que les fichiers utiles.


## cmake/

- `OmnisCheckNoImages.cmake` (71 l.) — Garde-fou (ctest « omnis_no_images ») : aucune image ne part avec le plug-in.
- `OmnisEmbedData.cmake` (26 l.) — Un tableau d'octets plutôt qu'une chaîne brute : MSVC limite les littéraux
- `OmnisRemoveFolderIcon.cmake` (24 l.) — Sous Windows, le SDK VST3 copie dans le paquet une icône de dossier (PlugIn.ico : le logo VST de Steinberg) et son desktop.ini. OMNIS ne livre aucune image ni le logo VST (une marque) : on les
- `OmnisVst3Sdk.cmake` (57 l.) — Deux modes :

## source/ai/

- `ConsoleActions.h/cpp` (357 l.) — Ce que la barre IA et le bouton AUTO font dans la console : une demande comprise (ai::understandRequest) et ce qu'OMNIS a entendu de la prod deviennent des réglages des modules, et une sonie à · types : ConsolePlan · fonctions : planRequest, planAuto, planReference, glueSettings, glueOf
- `DynamicsIntent.h/cpp` (176 l.) — Tenir la prod dans le temps comme la référence. · types : BandDynamics, DynamicsIntent · fonctions : planDynamics, dynamicsSettings, mbBandName
- `EmbeddedData.h` (26 l.) — Données livrées dans le binaire (générées depuis data/). · fonctions : lexicon, slang, references, catalogue, elements, i18n, styles, classement
- `Feedback.h/cpp` (512 l.) — Apprendre de chaque séance, chez soi puis pour tout le monde. · types : FeedbackRecord, LearningReport, CommunityRules · fonctions : serializeFeedback, parseFeedback, appendFeedback, loadFeedback, correctionFromCurves, parseCorrection, learnFromFeedback, exportAggregate, mergeAggregates
- `Lexicon.h/cpp` (917 l.) — Des mots de la personne aux axes sonores. · types : Descriptor, Concept, Lexicon, ConceptHit, Interpretation · enums : Axis · fonctions : axisKey, axisLabel, axisUnit, axisApplied, axisFromKey, parse, add, find, builtin, expandShorthand, interpret, isCommonWord
- `MatchIntent.h/cpp` (626 l.) — De la référence à une intention : ressembler, pas copier. · types : MatchRequest, Understanding, MatchIntent · fonctions : regionOfBand, requestFromInterpretation, parseRequest, understandRequest, planMatch, planFromWords, tonalDistanceDb
- `MixDiagnosis.h/cpp` (167 l.) — Diagnostic du mix : ce qu'OMNIS entend dans la prod (avant ses modules) et ce qu'il faut corriger DANS LE MIX (projet FL), en clair. Comparé à une référence (titre, style) ou, sans elle, à la moyenne des · types : MixFinding · enums : Level · fonctions : diagnoseMix, formatDiagnosis
- `ReferenceLibrary.h/cpp` (784 l.) — La base de références : artistes, titres, genres. · types : ReferenceEntry, ReferenceMatch, ReferenceLibrary, Neighbours, NameKey, EntryKeys · enums : Kind · fonctions : parse, add, builtin, of, index, aggregateSignature, addToLibraryFile, parseArtistLink, namesMatch
- `TextMatch.h/cpp` (197 l.) — Comprendre une demande écrite vite, avec des fautes. · fonctions : normalizeText, tokenize, phoneticKey, damerauLevenshtein, fuzzyEquals, isWord

## source/analysis/

- `BandLevels.h/cpp` (133 l.) — Niveau de chaque bande du multibande dans le temps. · types : BandLevels · fonctions : measureBandLevels
- `KWeightingFilter.h` (112 l.) — Pondération K (ITU-R BS.1770-4, §2.1). · types : BiquadCoefficients, BiquadState, KWeightingFilter · fonctions : process, reset, kWeightingShelf, kWeightingHighPass, prepare
- `LoudnessMeter.h/cpp` (355 l.) — Sonie ITU-R BS.1770-4 / EBU R128 (+ LRA EBU 3342). · types : LoudnessHistogram, LoudnessMeter · fonctions : allocate, reset, add, prepare, resetIntegrated, process, setStepCallback, powerToLufs, completeSubBlock
- `MusicDescriptors.h/cpp` (520 l.) — Ce qu'un morceau EST, au-delà de son spectre moyen : la fiche complète d'une référence (et de la prod qu'on travaille). Seulement des nombres, jamais d'audio. Analyse en flux (non temps réel). · types : MusicDescriptors, MusicAnalyzer, Biquad · fonctions : parseKeyCode, noteName, prepare, reset, push, run, endHop, endBlock, analyzeMusic
- `SignalStatistics.h` (87 l.) — Statistiques d'amplitude par canal. · types : SignalStatistics · fonctions : reset, add
- `SpectralSignature.h/cpp` (397 l.) — Ce qui fait durablement le son d'un mix. · types : SpectralSignature, SignatureAccumulator, NodeWeights, Frame, Workspace · fonctions : detectCutoff, prepare, reset, push, analyzeParallel, analyzeSignature
- `StereoMeter.h` (70 l.) — Corrélation de phase dynamique et ratio Mid/Side. · types : StereoMeter · fonctions : prepare, reset, process
- `TruePeakMeter.h/cpp` (212 l.) — Mesure de crête vraie (ITU-R BS.1770-4, Annexe 2). · types : TruePeakMeter · fonctions : prepare, reset, resetPeaks, process

## source/core/

- `AudioBuffer.h` (79 l.) — Tampon audio planaire double précision, aligné SIMD. · types : AudioBuffer, AlignedDelete · fonctions : allocate, assert, clear, channel, channels
- `Decibels.h` (37 l.) — Conversions amplitude <-> décibels. · fonctions : gainToDb, dbToGain, powerToDb, dbToPower
- `DelayLine.h` (75 l.) — Retard entier multicanal pour la compensation de latence. · types : DelayLine · fonctions : prepare, reset, setDelay, assert, process
- `I18n.h/cpp` (192 l.) — Langue de l'interface : français ou anglais. · enums : Lang · fonctions : language, setLanguage, overrideLanguageForTests, tr
- `OnePoleSmoother.h` (79 l.) — Lissage de paramètres par passe-bas à un pôle. · types : OnePoleSmoother · fonctions : prepare, setSnapThreshold, setTarget, snapTo, next, fill
- `Platform.h` (93 l.) — Détection plateforme, alignement, garde anti-dénormaux. · types : ScopedNoDenormals · fonctions : _mm_setcsr, __volatile__
- `SpscRingBuffer.h` (130 l.) — File circulaire sans verrou, 1 producteur / 1 consommateur. · types : SpscRingBuffer · fonctions : static_assert, allocate, push, pop
- `TripleBuffer.h` (78 l.) — Échange atomique d'un état complet entre deux threads. · types : TripleBuffer · fonctions : writeBuffer, publish, update, readBuffer
- `UserDataDir.h` (49 l.) — Dossier de données de la personne (hors thread audio). · fonctions : environmentVariable, userDataDir

## source/dsp/

- `Crossover.h` (151 l.) — Crossovers Linkwitz-Riley (LR4 / LR8) et réseau N bandes. · types : LinkwitzRileyAllpass, LinkwitzRileyCrossover, CrossoverNetwork · enums : CrossoverSlope · fonctions : setup, reset, process, assert
- `Fft.h/cpp` (135 l.) — FFT complexe radix-2 en double précision, sans dépendance. · types : Fft · fonctions : cmul, prepare
- `FirKernelBank.h` (150 l.) — Banque de noyaux FIR sans verrou (thread audio / calcul). · types : FirKernelBank, Slot · fonctions : prepare, stop, request, takeReady, initialSlot, markRetiring, release, acquireForWriting, workerLoop
- `Halfband.h/cpp` (277 l.) — Filtres demi-bande polyphase IIR (méthode de L. de Soras, HIIR). · types : AllpassChain, Halfband2x · fonctions : designHalfband, setCoefficients, reset, process, upsample, downsample, upsampleBlock, downsampleBlock
- `LinearPhaseDesigner.h/cpp` (123 l.) — FIR à phase linéaire depuis une réponse en module. · types : LinearPhaseDesigner · fonctions : prepare, design, finish
- `LookaheadGain.h` (95 l.) — Lissage de gain « brickwall » à anticipation. · types : LookaheadGain · fonctions : prepare, reset, process
- `Oversampler.h/cpp` (179 l.) — Suréchantillonnage stéréo 1x … 16x, cascade de demi-bandes. · types : Oversampler · fonctions : prepare, reset, setStages, upsample, downsample
- `StereoConvolver.h/cpp` (264 l.) — Convolution FFT partitionnée uniforme (UPOLS), 2 canaux. · types : StereoConvolver, Kernel, KernelBuilder · fonctions : allocate, prepare, reset, build, write, processFrame, accumulate
- `SvfFilter.h` (132 l.) — Filtre d'état variable à topologie préservée (TPT SVF). · types : SvfCoefficients, SvfState, SvfFilter · enums : SvfType · fonctions : make, reset, process, setCoefficients, set, butterworthQ
- `Waveshaper.h` (140 l.) — Courbes de clipping et anti-repliement par antidérivée. · types : AdaaClipper, Output · enums : ClipShape · fonctions : hard, hardIntegral, softKnee, softKneeIntegral, diode, diodeIntegral, reset, process

## source/engine/

- `Engine.h/cpp` (709 l.) — Moteur DSP : chaîne d'étages, PDC, bypass, mesures. · types : MeterReadout, Engine, Slot · fonctions : appendStage, setRealtimeMode, prepare, reset, setParameter, setParameterImmediate, process, meterOnly, setLowLatency, forceListenSim, resetLoudnessHistory, sanitizeInput, applyParameter, processChunk
- `ListenSim.h/cpp` (358 l.) — Écoutes simulées : entendre le master comme sur des écouteurs, un téléphone, TikTok, une voiture, un club, une plateforme (normalisation de la sonie) ou un rendu spatial binaural. · types : ListenSim, Biquad · enums : Filter · fonctions : labels, prepare, reset, setPreset, process, run, design, responseDb, build
- `MasteringChain.h/cpp` (53 l.) — Construction de la chaîne de mastering complète. · fonctions : buildMasteringChain
- `ProdListener.h/cpp` (215 l.) — Le master écoute la prod qui passe (son entrée), en continu pendant la lecture : la barre IA et AUTO partent de ce qu'il a entendu. Seuls des nombres sont gardés (signature), jamais d'audio. · types : ProdListener, Snapshot · enums : AutoState · fonctions : prepare, release, push, forget, startAuto, cancelAuto, takeAutoResult, loop
- `ReferenceCapture.h/cpp` (256 l.) — Écouter une référence jouée dans le DAW. · types : ReferenceCapture, Result, Frame · fonctions : prepare, release, setCompletionHandler, setMinimumSeconds, setCapturing, push, workerLoop, drain
- `Stage.h` (75 l.) — Interface d'un étage de la chaîne de mastering. · types : ProcessSpec, Stage · fonctions : lowLatencyLength, setParameter, setRealtime, setPeakGuard

## source/gfx/

- `AvatarCache.h` (97 l.) — Où vivent les illustrations (avatars d'artistes, pochettes) : le cache de l'utilisateur, jamais le plug-in ni l'installateur. · types : Colors · fonctions : reduced, hashName, root, artistPath, coverPath, colorsPath, writeColors, out, readColors, in, existing
- `OmnisAvatarStylizer.h/cpp` (866 l.) — Une photo d'artiste ou une pochette (octets JPG / PNG : API publique interrogée par le poste de la personne, ou image intégrée à un FLAC qu'elle a scanné) devient l'avatar 256 × 256 RGBA de la console. · types : RgbaImage, OmnisAvatarStylizer, Options, Rgb, Box, Primitive, Emblem · enums : Style, Shape, Kind · fonctions : cropCenter, findCornerText, inpaint, denoise, kuwahara, grade, celShade, inkMask, fitShapes, renderEmblem, dominantColors, savePng, toNanoVG

## source/link/

- `LinkBus.h/cpp` (631 l.) — Les instances d'un projet travaillent ensemble. · types : StemOrders, StemView, Header, Endpoint, StemListener, Heard, State · enums : Role · fonctions : stemKindName, regionName, nowMs, join, leave, heartbeat, setKind, setName, writeAudio, writeOrders, isLive, poll
- `LinkClient.h/cpp` (477 l.) — Une instance du plug-in dans son groupe. · types : LinkClient, Listening, PendingProposal, Applied, RtAccess, StemFixes · enums : Answer · fonctions : start, stop, setKind, setTrackName, rtListening, pushListening, setGroup, answer, setConductorPeriodMs, run, conduct, handleAnswers, sendCorrections, publish
- `SharedRegion.h/cpp` (136 l.) — Mémoire partagée nommée entre instances du plug-in. · types : SharedRegion · fonctions : open, close, unlink

## source/params/

- `Parameters.h/cpp` (662 l.) — Table unique des paramètres (DSP, VST3, outils, IA). · types : ParamSpec · enums : ParamId, ParamKind, ParamScale, MbControl, MonitorMode, MonoSlope, LinkRole, PhaseMode, Module · fonctions : matchBandHz, matchMidId, matchSideId, mbId, allParameters, findParameter, findParameterByKey, parameterIndex, toPlain, toNormalized, clampPlain, isPersistent, moduleOnId, chainOrder

## source/plugin/vst3/

- `OmnisController.h/cpp` (1097 l.) — Contrôleur d'édition VST3 (thread UI). · types : ProposalLine, ReferenceTitle, ReferenceArtist, ReferenceStyle, ReferenceTree, FamilyFilter, ReferenceCard, UndoStep · enums : RefKind · fonctions : createInstance, requestProposals, answerProposal, requestFeed, editParameter, beginGesture, performGesture, endGesture, ask, mixDiagnosis, startAuto, restartTutorial, pollAutomation, setPromoMode
- `OmnisEditor.h/cpp` (2470 l.) — La console OMNIS (VSTGUI, dessinée en code), d'après la maquette console v11 : · types : OmnisEditor : Steinberg::Vst::VSTGUIEditor · fonctions : buildInto, tick, selectPage, pumpFeed, startTutorial, setPlain, setNorm, buildRibbons, buildRack, buildDrawer, buildScreens, buildMaster, page, knobs
- `OmnisFactory.cpp` (38 l.) — Point d'entrée du module VST3.
- `OmnisIds.h` (48 l.) — Identifiants de classes VST3.
- `OmnisProcessor.h/cpp` (904 l.) — Composant audio VST3 (thread temps réel). · types : OmnisProcessor : Steinberg::Vst::AudioEffect, ParamEvent, GateHold · fonctions : createInstance, prepareEngine, collectParameterEvents, applyParameter, applyPendingState, handleTransport, publishMeters, processSegments, onCaptureFinished, processStem, switchRole, passThrough, feedBlock, publishFeed
- `OmnisState.h/cpp` (121 l.) — Format binaire de l'état du processeur. · fonctions : writeState, readState, writeExtras, readExtras
- `OmnisVersion.h` (27 l.) — Identité et version du plug-in.
- `UiFeed.h/cpp` (108 l.) — Ce que le processeur montre à l'interface, sans passer par les paramètres de sortie (trop lents et trop maigres pour un spectre ou un goniomètre). · types : UiMeters, UiFeed · fonctions : registerFeed, unregisterFeed, findFeed

## source/plugin/vst3/gui/

- `Look.h` (149 l.) — L'apparence de la console (maquette v11) : face en aluminium anodisé sombre, découpes usinées, écrans noirs, ambre réservé aux signaux actifs. Polices du système (pas de fichier de police à livrer). · fonctions : kVoid, kMetal, kMetal2, kMetalTop, kInset, kScreen, kEdgeHi, kEdgeLo, kLine, kText, kText2, kText3, kAccent, kAccentHot
- `OpenUrl.h/cpp` (76 l.) — Ouvrir une page web dans le navigateur (le lien d'une référence). À part : <windows.h> ne touche pas le reste de la console. · fonctions : openUrl
- `Widgets.h/cpp` (670 l.) — Les pièces de la console, dessinées en code : - LedKnob : bouton rotatif à couronne de LED (allumées du repos à la valeur), valeur affichée dessous ; glisser verticalement (Maj : fin), · types : Live, Model, Callbacks · enums : Size · fonctions : setNorm, setBlink, setLabel, invalid, setAccentText, setMouse, markDirty, setNumber, setDimmed, setDropMark

## source/stages/

- `ClipperStage.h/cpp` (501 l.) — Étage 5 : Smart Wave-Shaping / Mastering Clipper. · types : SplitKernelRequest, SplitKernelDesigner, ClipperStage : Stage, Band · fonctions : prepare, design, splitKernelLengthFor, applyOversamplingChoice, splitFrameBoundary, clipBand
- `ImagerStage.h/cpp` (239 l.) — Étage 4 : Imager (largeur stéréo multibande). · types : ImagerKernelRequest, ImagerKernelDesigner, ImagerStage : Stage · fonctions : prepare, design, kernelLengthFor, frameBoundary
- `InflatorStage.h/cpp` (408 l.) — Étage 3 : Inflator (densité et chaleur). · types : InflatorCurve, InflatorStage : Stage, Band · fonctions : exponentFor, shape, slope, splitKernelLengthFor, applyOversamplingChoice, splitFrameBoundary, harmonicsOf
- `LimiterStage.h/cpp` (355 l.) — Étage 6 : Lookahead Brickwall Limiter (true peak). · types : LimiterStage : Stage · fonctions : detect
- `MatchEqStage.h/cpp` (160 l.) — Étage 2 : Match EQ M/S à phase linéaire. · types : MatchEqStage : Stage · fonctions : frameBoundary
- `MatchKernelBank.h/cpp` (111 l.) — Noyaux FIR à phase linéaire du Match EQ. · types : MatchKernelRequest, MatchKernelDesigner · fonctions : matchCurveDb, prepare, design
- `MultibandStage.h/cpp` (429 l.) — Étage 4 : dynamique multibande (« tenir la 808 »). · types : MbKernelRequest, MbKernelDesigner, MultibandStage : Stage, Band · fonctions : prepare, design, kernelLengthFor, updateKneeStart, updateTimeConstants, frameBoundary
- `SubControlStage.h/cpp` (403 l.) — Étage 1 : Sub Control & Mono Maker. · types : SubControlStage : Stage · fonctions : kernelLengthFor, blockSizeFor, updateAnalogCoefficients, frameBoundary, processSynth
- `SubKernelBank.h/cpp` (100 l.) — Noyaux FIR à phase linéaire du Sub Control. · types : SubKernelRequest, SubKernelDesigner · fonctions : prepare, design

## source/stems/

- `Conductor.h/cpp` (330 l.) — Le master chef d'orchestre : il repère qu'un stem en gêne un autre et PROPOSE une retouche. Rien n'est appliqué sans accord. · types : ConductorStem, Proposal, Band, Conductor · enums : Kind
- `Correction.h` (45 l.) — Une retouche que le master demande à un stem. · types : Correction · enums : Type · fonctions : clampCorrection, anyCorrection
- `DrumBus.h/cpp` (234 l.) — Ce qu'il y a dans un bus de drums : kick, snare / clap, hi-hat, open hat, reconnus à leurs coups (thread du lien, jamais l'audio). · types : DrumContent, Item, DrumBusAnalyzer, Biquad · fonctions : describeDrumBus, prepare, reset, push, run, endFrame
- `Element.h/cpp` (301 l.) — Ce que joue une piste « stem ». · types : NameHint · enums : Element, Family · fonctions : familyOf, familyLabel, elementLabel, elementKey, elementFromKey, elementChoiceLabels, elementMeterLabels, nameWords, elementFromName
- `ElementClassifier.h/cpp` (489 l.) — Reconnaître ce que joue une piste en l'écoutant. · types : Character, Identification, ElementClassifier, Frame · fonctions : prepare, reset, push, analyseFrame, combine
- `StemChain.h/cpp` (564 l.) — La chaîne d'un stem, sans aucune latence. · types : StemChain, Smoothed, MasterFix, DynBand · fonctions : prepare, reset, setSettings, process, control, processChunk, snapToTarget
- `StemSettings.h/cpp` (322 l.) — Ce que la chaîne d'un stem applique, selon l'élément. · types : DynamicBand, StemSettings, StemMacros · enums : StemMode · fonctions : pinkBalanceDb, templateFor, settingsFor, clampSettings
- `StemTimeline.h/cpp` (214 l.) — Ce que le master a entendu d'un stem, et QUAND. · types : StemTimeline, Block · fonctions : prepare, reset, push, flush, analysePending

## tests/

- `TestAvatarStylizer.cpp` (255 l.) — L'illustration d'une pochette : macaron du coin retiré, grain effacé, teinte gardée, trait sur les contours, détourage. · 9 tests : avatar HD: 256 px, macaron du coin retiré, couleurs d'origine, fondu s ; avatar peint (option): macaron retiré, teinte gardée, détourage radial ; avatar emblème: des formes posées d'après l'image (pas de filtre), ron ; emblème: chaque forme rapproche du modèle (un disque est retrouvé) ; avatar: un bloc de texte noir et blanc est repéré dans un coin, jamais ; avatar: les deux couleurs dominantes (monogramme du mode promo) ; …
- `TestConductor.cpp` (210 l.) — Le master chef d'orchestre : écoute alignée sur le temps du projet, masquages, propositions, retouches appliquées. · 5 tests : chef d'orchestre: frise rangée au temps du projet (morceaux, décalage, ; chef d'orchestre: le piano qui couvre la 808 au même moment -> couper  ; chef d'orchestre: pas de fausse alerte (piano sans grave, ou qui joue  ; chef d'orchestre: une guitare qui couvre la voix dans sa présence -> l ; chef d'orchestre: la retouche acceptée s'applique au stem (même en Off
- `TestConvolution.cpp` (133 l.) — FFT, convolution UPOLS, noyaux à phase linéaire. · 3 tests : fft: identique à la DFT directe, aller-retour exact (N = 8 … 4096) ; upols: deux canaux = convolutions directes, latence exacte B, changeme ; noyaux sub: réponse conforme à la cible, phase linéaire (symétrie), Mi
- `TestCore.cpp` (191 l.) — Primitives temps réel. · 9 tests : smoother: 99 % atteint au temps de lissage, sans dépassement ; smoother: colle exactement à la cible et s'arrête ; smoother: temps nul = saut immédiat ; spsc: sémantique FIFO, bouclage et file pleine ; spsc: concurrence producteur/consommateur, 2 millions d'éléments ordon ; triple buffer: le lecteur ne voit jamais d'état déchiré, versions croi ; …
- `TestDeezer.cpp` (76 l.) — Lecture des réponses de l'API Deezer (omnis_refdb deezer) : sur des réponses d'exemple au format de l'API, sans réseau. · 3 tests : deezer: lecteur JSON (échappements, unicode, imbrication) ; deezer: le bon artiste (accents, fautes) et le bon titre (feat. ignoré ; deezer: adresses de recherche encodées, adresses refusées avant curl
- `TestDrumBus.cpp` (116 l.) — Reconnaître les éléments d'un bus de drums. · 3 tests : bus de drums: kick, snare, hi-hat et open hat reconnus dans un bus com ; bus de drums: ce qui ne joue pas n'est pas inventé (kick seul, hats se ; bus de drums: la chaîne du bus suit ce qui est reconnu (snare plus lar
- `TestDynamics.cpp` (399 l.) — Courbes ADAA, clipper suréchantillonné, limiteur. · 14 tests : courbes: pente 1 à l'origine, bornées, antidérivées cohérentes (dF/du  ; clipper: transparent sous le genou (-12 dBFS : gain 0 dB, distorsion < ; clipper: repliement d'un écrêtage dur sévère selon le suréchantillonna ; clipper: plafond respecté (dépassement < 0.4 dB en deux bandes) et DC  ; clipper deux bandes: la 808 écrêtée n'intermodule plus le haut du spec ; limiteur: aucun échantillon ni crête vraie au-dessus du plafond (+12 d ; …
- `TestEngine.cpp` (249 l.) — Moteur : null-test, PDC, bypass, monitoring. · 8 tests : engine: chaîne neutre = null-test bit-exact (64 et 32 bits) ; engine: latence = somme des étages, bypass aligné et sans clic ; engine: gain de sortie +6.02 dB = x2 après lissage ; engine: monitoring Side sur signal mono = silence, Mid sur anti-phase  ; engine: les mesures portent sur la sortie (avant monitoring) ; engine: sortie indépendante du découpage en blocs (lissage déterminist ; …
- `TestFilters.cpp` (163 l.) — SVF TPT et crossovers Linkwitz-Riley. · 5 tests : svf: passe-bas / passe-haut Butterworth = -3.01 dB à fc, pentes 12 dB/ ; svf: cloche, plateaux, passe-tout, réjecteur ; svf: stable sous modulation brutale de fc et Q (bruit, 1 s) ; linkwitz-riley: -6.02 dB par voie à fc, somme = passe-tout exact (LR4, ; crossover network: 6 bandes, somme plate à ±1e-6 dB, bandes isolées
- `TestFramework.h` (70 l.) — Micro-framework de tests sans dépendance. · types : TestCase, Registrar · fonctions : registry, failureCount, reportFailure
- `TestI18n.cpp` (32 l.) — L'interface en anglais : textes entiers, morceaux dans un texte composé, formats remplis après traduction, diagnostic. · 1 tests : anglais: textes entiers, morceaux, formats ; français inchangé
- `TestInflatorImager.cpp` (253 l.) — Étages 3 (Inflator) et 4 (Imager). · 8 tests : inflator: courbe (g(0) = 0, g(1) = 1 sans cassure, pente à l'origine m ; inflator: réglages par défaut = entrée retardée, au bit près ; inflator: densité sans crête plus haute (mix 100 %, courbe par défaut  ; inflator: chaleur = harmoniques paires, sans continu ; 4x : pas de rep ; inflator: « Inflate 808 » ne touche que le bas (50 Hz gonflé, 1 kHz in ; imager: largeurs à 100 % = entrée retardée (1e-12) ; …
- `TestLanguage.cpp` (505 l.) — Comprendre la demande (fautes, nuances, références). · 22 tests : texte: fautes de frappe et d'orthographe comprises, sans faux amis ; lexique: la base livrée se lit sans erreur et couvre les nuances ; demande: « ça manque de crispy », « c'est trop brillant », « trop prop ; demande: un défaut nommé est une plainte, « trop » ou pas ; demande: négations (« c'est pas propre » = veut du propre, « pas de di ; demande: intensité (« un poil », « vraiment »), protection (« garde ma ; …
- `TestLearning.cpp` (106 l.) — Apprentissage local et communautaire. · 5 tests : retours: journal relu à l'identique ; apprentissage local: « crispy » s'ajuste aux corrections, un défaut da ; apprentissage local: un mot inconnu devient un concept s'il est cohére ; communauté: export anonyme, fusion, seules les tendances confirmées en ; retours: correction déduite des courbes proposée / gardée
- `TestLink.cpp` (247 l.) — Instances liées : master et stems d'un même groupe. · 6 tests : lien: un master voit ses stems (rôle, type, nom), pas ceux d'un autre  ; lien: le master écoute l'audio d'un stem pendant la lecture (même sign ; lien: ordres du master -> stem (jeton vérifié), reprise d'un emplaceme ; lien: stem qui écrit et master qui écoute en même temps ; changement d ; lien: un stem se reconnaît (nom de piste, puis écoute) et le master le ; lien: les retouches acceptées voyagent dans les ordres ; un nouveau ve
- `TestListenSim.cpp` (87 l.) — Écoutes simulées : courbes, spatial, plateformes, et jamais dans un export. · 4 tests : écoute simulée: les étiquettes suivent les préréglages ; écoute simulée: téléphone sans grave, AirPods ouverts avec peu de sub, ; écoute simulée: « Off » ne touche à rien, le spatial reste fini et du  ; écoute simulée: une plateforme baisse un master fort vers sa cible, ja
- `TestLoudness.cpp` (123 l.) — Conformité ITU-R BS.1770-4 / EBU Tech 3341 & 3342. · 6 tests : k-weighting: coefficients 48 kHz = Tables 1 et 2 de BS.1770-4 ; ebu3341: cas 1 et 2 (M, S, I) à 48 et 44.1 kHz ; ebu3341: cas 3, 4, 5 (portes absolue et relative) ; loudness: indépendance vis-à-vis du découpage en blocs ; loudness: silence -> -inf, aucune mesure intégrée ; ebu3342: LRA d'un signal stationnaire ~ 0, d'un signal à deux niveaux 
- `TestMain.cpp` (43 l.) — tests/TestMain.cpp
- `TestMatchEq.cpp` (177 l.) — Étage Match EQ (courbe d'intention M/S). · 6 tests : match eq: courbe Catmull-Rom (nœuds exacts, pente régulière reproduite ; match eq: courbe nulle = retard pur (null-test < 1e-12) ; match eq: nœud Mid +6 dB à 1.28 kHz, Side +3 dB à 5.12 kHz, voisins lo ; match eq: Match Amount dose la courbe (50 % de -6 dB à 40 Hz = -3 dB) ; match eq: changement de courbe en lecture sans discontinuité ; match eq: tilt +3 dB (pivot 640 Hz) : -2.4 dB à 40 Hz, 0 à 640 Hz, +2.
- `TestMatchIntent.cpp` (240 l.) — Signature, intention d'adaptation, demande. · 10 tests : signature: bruit rose = balance plate (±0.6 dB de 28 Hz à 14 kHz) ; signature: le Match EQ appliqué se retrouve dans la signature (pente + ; signature: une autre note de 808 (41 vs 49 Hz) ne déclenche pas d'EQ ( ; signature: les silences ne comptent pas (moitié muette = même balance) ; intention: écart partiel, sub protégé (±3 dB), bande vide jamais boost ; intention: jamais de copie (ressemblance bornée à 85 %) ; …
- `TestMixDiagnosis.cpp` (71 l.) — Le diagnostic du mix voit ce que le master ne rattrape pas, et ne fait de la 808 hors gamme qu'une remarque. · 2 tests : diagnostic: un mix qui écrête déjà, un grave en opposition de phase ; diagnostic: une 808 hors gamme n'est qu'une remarque (parfois voulue)
- `TestMultiband.cpp` (138 l.) — Dynamique multibande. · 5 tests : multibande: réglages neutres = retard pur (null-test < 1e-12) ; multibande: compensation d'une bande = sa réponse Linkwitz-Riley (±0.0 ; multibande: courbe statique (30 Hz à -10 dBFS RMS, seuil -20, ratio 4  ; multibande: 808 tenue proprement (harmoniques < -50 dB) et voix intact ; multibande: anticipation — un coup de 808 ne dépasse pas sa valeur ten
- `TestMusic.cpp` (149 l.) — La fiche complète d'un son (analysis/MusicDescriptors) : ce qu'elle mesure, comment elle s'écrit dans la base, ce qu'OMNIS en fait. · 4 tests : fiche: tempo, note de la 808, tonalité et stéréo d'une boucle trap ; fiche: deux canaux sans rapport = corrélation nulle, perte en mono ; u ; fiche: écrite dans la base puis relue à l'identique (et une fiche anci ; fiche: OMNIS s'en sert — une réf qui attaque plus donne du Punch, un g
- `TestOversampling.cpp` (149 l.) — Demi-bandes HIIR et cascade 2x … 16x. · 6 tests : halfband: 120 dB / tw 0.02 -> 12 coefficients, coefficients dans ]0, 1 ; oversampling: aller-retour plat à ±0.001 dB jusqu'à 20 kHz (2x … 16x) ; oversampling: images rejetées à > 115 dB après la montée 8x ; oversampling: contenu ultrasonore rejeté à > 115 dB à la descente (ant ; oversampling: retard de phase à 1 kHz faible (< 8 échantillons) et 1x  ; halfband: traitement par bloc identique au bit près au traitement écha
- `TestParameters.cpp` (93 l.) — Table de paramètres. · 6 tests : params: identifiants et clés uniques, défauts dans les bornes ; params: aller-retour normalisé <-> plain ; params: convention VST3 des valeurs discrètes ; params: formatage et saisie texte ; params: indépendant de la locale de l'hôte (virgule décimale) ; params: échelle logarithmique des fréquences
- `TestPluginAi.cpp` (343 l.) — La barre IA et AUTO dans le plug-in : le vrai processeur écoute la prod, le vrai contrôleur comprend la demande, règle les modules et mène le volume à la cible par la mesure. · 9 tests : plug-in barre IA: « plus de punch, plus fort, garde ma 808 » règle les ; plug-in AUTO: écoute 10 s du passage joué, règle la chaîne vers le sty ; plug-in menu RÉFÉRENCE: styles, artistes, titres de la base ; un titre ; plug-in mode promo: « /fbi » bascule les monogrammes, gardé dans le do ; plug-in: « remets tout à zéro » ramène chaque réglage à son départ ; plug-in: « annule » défait la dernière action (ici la remise à zéro),  ; …
- `TestPluginLink.cpp` (386 l.) — Deux instances du plug-in liées : un master et un stem, comme sur le bus master et une piste de FL Studio. · 5 tests : plug-in lié: le master voit le stem, le stem n'a aucune latence et app ; plug-in stem: le nom de piste de l'hôte et l'écoute donnent l'élément  ; plug-in stem: la chaîne de l'élément agit sans latence (Auto), Manuel  ; plug-in chef d'orchestre: le master propose, la personne accepte, le p ; plug-in: le rôle changé en route s'applique sans réactivation (FL Stud
- `TestPluginSlotToggle.cpp` (216 l.) — Le bouton rond d'un slot du mixer de FL Studio : l'hôte désactive / réactive l'instance pendant que son thread audio continue d'appeler process(). Rien ne doit planter, ni sortir autre · 3 tests : slot FL: activer / désactiver pendant process() (Seul) ne plante pas ; slot FL: activer / désactiver un master et un stem liés pendant proces ; slot FL: clics sur le slot mêlés à des changements de rôle en route ne
- `TestPluginState.cpp` (200 l.) — État du processeur VST3 (aller-retour complet). · 3 tests : plug-in: getState(setState(x)) = x pour les 90 paramètres persistants ; plug-in: la référence jouée sur l'entrée auxiliaire entre dans la base ; plug-in: une écoute de moins de 60 s n'est pas retenue
- `TestRack.cpp` (377 l.) — Le rack : modules allumés / éteints, ordre, écoute Delta, Gain Match ; et le limiteur (Punch, upward, lien stéréo). · 11 tests : rack: ordre des modules, rang <-> permutation pour les 5040 ordres ; rack: tous les modules éteints = entrée retardée de la latence, bit à  ; rack: éteindre un module en route = fondu sans clic vers le même son q ; rack Delta: on n'entend que ce que le module change (limiteur en fin d ; rack: réordonner en route passe au son sec puis rejoint le nouvel ordr ; rack Gain Match: on écoute au volume de l'entrée, les mesures gardent  ; …
- `TestReferenceCapture.cpp` (106 l.) — Écoute d'une référence pendant la lecture. · 3 tests : capture: signature de la référence jouée = signature du fichier (±0.01 ; capture: rien n'est poussé hors écoute, et une file pleine perd des éc ; capture: écoute trop courte gardée, la suivante s'y ajoute ; retenue a
- `TestStemChain.cpp` (432 l.) — La chaîne d'un stem : réglages par élément et son. · 10 tests : stem chaîne: Off laisse passer au bit près, aussi après avoir agi ; stem chaîne: aucune latence (le son direct sort au même échantillon) ; stem chaîne: le passe-haut retire le grondement, garde le médium ; stem chaîne: grave en mono, largeur au-dessus, Mid intact ; stem chaîne: l'EQ dynamique calme ce qui déborde par moments, respecte ; stem chaîne: compression des drums, coups égalisés, niveau de la piste ; …
- `TestStems.cpp` (296 l.) — Reconnaître les éléments des stems (nom et écoute). · 5 tests : stems: nom de piste -> élément (noms de packs, abréviations, collés) ; stems: l'écoute reconnaît kick, 808, snare, clap, hats, piano, nappe,  ; stems: sons saturés ou « bizarres » toujours reconnus, caractère mesur ; stems: écoute et nom combinés (le nom précise, l'écoute nette l'emport ; stems: le paramètre « Element » et l'afficheur suivent la liste des él
- `TestSubControl.cpp` (299 l.) — Étage Sub Control & Mono Maker. · 11 tests : mono maker: Side à 40 Hz supprimé (LR4 -37 dB, LR8 -70 dB), Side à 1 k ; mono maker: un signal mono reste mono (|L − R| < 1e-12) ; mono maker: image au-dessus de f_mono intacte (L seul à 2 kHz -> R res ; mono maker: amount 0 % = passe-tout (module plat L et R) ; low cut: Butterworth 24 dB/oct, -3.01 dB à fc, DC supprimé ; sub shelf: +6 dB sous la fréquence, 0 dB au-dessus ; …
- `TestTruePeak.cpp` (87 l.) — Crête vraie (BS.1770-4 Annexe 2). · 5 tests : true peak: sinus fs/4 à 45° -> sample peak -3.01, true peak 0.00 dBTP ; true peak: sinus 997 Hz 0 dBFS -> 0 dBTP, jamais sous le sample peak ; true peak: phase 0 = identité exacte ; true peak: interpolateur plat (+/-0.01 dB) jusqu'à 0.45 fs ; true peak: crêtes inter-échantillons d'un signal transitoire >= sample
- `TestWav.cpp` (118 l.) — Entrées/sorties WAV et signaux de test. · 4 tests : wav: aller-retour f64 exact, f32 / pcm24 / pcm16 à la quantification p ; signaux: tous les générateurs produisent un signal fini et non vide ; signaux: niveaux annoncés vérifiés par l'analyseur ; sweep: sweep ⊗ inverse = réponse plate (0 dB ±0.5) dans la bande

## tools/analyze/

- `main.cpp` (59 l.) — tools/analyze — Rapport de métriques d'un ou plusieurs fichiers WAV.

## tools/ask/

- `main.cpp` (87 l.) — tools/ask — Ce que le plug-in comprend d'une demande (et retour d'écoute).

## tools/codemap/

- `codemap.py` (96 l.) — classes, fonctions publiques), pour qu'un agent trouve où agir sans relire

## tools/common/

- `AiFiles.h` (54 l.) — Bases livrées + fichiers utilisateur. · fonctions : readTextFile, in, writeTextFile, out, loadLexicon, loadLibrary
- `AnalysisReport.h/cpp` (346 l.) — Rapport de métriques d'un fichier audio. · types : ChannelReport, BandReport, AnalysisReport · fonctions : analyze, analysisBandEdges
- `AudioRead.h/cpp` (141 l.) — Lire un son quel que soit son format. · types : EmbeddedPicture · fonctions : readAudioFile, isReadableAudio, readEmbeddedPictures, readFlacPictures
- `Deezer.h/cpp` (699 l.) — Images et liens d'une référence depuis l'API publique de Deezer (api.deezer.com, sans clé) : photo de l'artiste, pochette de chaque titre, lien vers la musique. Utilisé par omnis_refdb, sur le PC de la · types : Json, Artist, Track, Album, Page, TrackDetail, Fetcher · enums : Type · fonctions : parseJson, parseArtists, parseTracks, parseAlbums, parseArtistList, parseArtist, parseTrackDetail, parseAlbumGenre, pickArtist, sameArtist, pickTrack, urlEncode, bareTitle, artistSearchUrl
- `DistortionAnalysis.h/cpp` (223 l.) — Mesure objective de la distorsion ajoutée. · types : DistortionBand, DistortionReport · fonctions : measureDistortion
- `LoudnessTuner.h/cpp` (285 l.) — Atteindre une sonie cible avec le moins de distorsion possible. · types : TuningStrategy, TuningResult, TuningOptions · fonctions : loudestExcerpt, renderWithSettings, tuneStrategy, loudestClean, settingsOf
- `TestSignals.h/cpp` (572 l.) — Générateurs de signaux de mesure. · types : SweepResult · fonctions : logSweep, pinkNoise, whiteNoise, impulse, levelSteps, drumLoop, intersamplePeak, twoTone, ebu3341, stereoImaging, applyFades, availableSignals, generate
- `TonalBalance.h/cpp` (134 l.) — L'équilibre tonal de la prod est-il respecté ? · types : TonalBand, TonalReport · fonctions : compareTonalBalance
- `WavFile.h/cpp` (280 l.) — Lecture / écriture WAV minimale et sans dépendance. · types : AudioFile · enums : SampleFormat · fonctions : utf8Path, resize, readWav, writeWav, parseSampleFormat

## tools/compare/

- `main.cpp` (46 l.) — tools/compare — Distorsion non linéaire ajoutée par un traitement.

## tools/guishot/

- `main.cpp` (198 l.) — Rend la console OMNIS hors écran (PNG), sans DAW : le vrai processeur tourne sur une boucle de test, le vrai contrôleur et la vraie interface lisent ce qu'il mesure. Sert à vérifier l'interface page

## tools/learn/

- `main.cpp` (93 l.) — tools/learn — Apprentissage (local, puis communautaire). · commandes : local, export, fusion, communaute

## tools/match/

- `main.cpp` (254 l.) — tools/match — « Fais sonner ma prod comme ce titre, sans la détruire. »

## tools/refdb/

- `discographies_a_mesurer.py` (92 l.) — data/sons_a_mesurer.csv, ses sons populaires d'abord puis le reste, jusqu'à la moitié
- `liste_500.py` (56 l.) — l'entrée auxiliaire, aucun fichier) : les 190 premiers titres déjà faits
- `main.cpp` (1349 l.) — tools/refdb — Base de références (signatures, jamais d'audio). · commandes : image, pochette, deezer, embleme, cartoon, discographie, fiche, add, scan, titres, artistes, csv, nom, noms, list, find
- `sons_a_mesurer.py` (114 l.) — dans un budget de 28 Go de FLAC (la scène demandée, puis tes playlists et les proches

## tools/render/

- `main.cpp` (176 l.) — tools/render — Rendu hors ligne d'un WAV à travers le moteur OMNIS.

## tools/stems/

- `main.cpp` (283 l.) — tools/stems — Ce qu'OMNIS reconnaît et fait sur des stems exportés.

## tools/testgen/

- `main.cpp` (117 l.) — tools/testgen — Générateur du kit de signaux de test. · commandes : kit, list

## tools/tune/

- `main.cpp` (113 l.) — tools/tune — Recherche du réglage le plus propre pour une sonie cible.
