#include <LittleFS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <hardware/watchdog.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SDA_PIN 28
#define SCL_PIN 29

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

#define MAX_INVENTORY 6

enum PieceType { Empty,
                 Pawn,
                 Rock,
                 Bishop,
                 King,
                 Gold };

struct CellItem {
  PieceType Piece = Empty;
  int TeamID = 0;

  CellItem(PieceType Piece = Empty, int TeamID = 0)
    : Piece(Piece), TeamID(TeamID) {}

  CellItem& operator=(const CellItem& Other) {
    if (this != &Other) {
      Piece = Other.Piece;
      TeamID = Other.TeamID;
    }
    return *this;
  }

  static void swap(CellItem& First, CellItem& Second) {
    if (&First != &Second) {
      CellItem Temp = First;
      First = Second;
      Second = Temp;
    }
  }

  static void swapInventory(CellItem& InventorySlot, CellItem& CellSlot) {
    if (&InventorySlot != &CellSlot) {
      CellItem Temp = InventorySlot;
      InventorySlot = CellSlot;
      CellSlot = Temp;
      switch (InventorySlot.TeamID) {
        case 1: InventorySlot.TeamID = 2; break;
        case 2: InventorySlot.TeamID = 1; break;
      }
      if (InventorySlot.Piece == Gold) {
        InventorySlot.Piece = Pawn;
      }
    }
  }
};

int GameType = -1;
int PlayerScore[3] = { 0 };

struct RestStateClass {
  int PlayerTurn = -1;
  CellItem cells[12] = { CellItem() };
  CellItem inventory[6] = { CellItem() };
};

struct GameState {
  int gameType = 0;            // Game type as an integer
  int playerScore[3] = { 0 };  // Array of player scores
  bool bPlayerCaused = false;  // bPlayerCaused

  RestStateClass RestState;  // state to undo game state
};

// Global GameState instance for saving/loading
GameState GameSaves;

void SaveGame() {
  if (LittleFS.begin()) {
    File file = LittleFS.open("/save.txt", "w");
    if (file) {
      file.write(reinterpret_cast<const uint8_t*>(&GameSaves), sizeof(GameSaves));
      file.close();
      //Serial.println("Saved");
    } else {
      Serial.print("Failed to save game");
    }
  }
}

bool LoadGame() {
  if (LittleFS.begin()) {
    File file = LittleFS.open("/save.txt", "r");
    if (file) {
      file.read(reinterpret_cast<uint8_t*>(&GameSaves), sizeof(GameSaves));
      file.close();
      //Serial.println("GameLoaded");
      return true;
    } else {
      Serial.print("Failed to load game");
    }
  }

  return false;
}

void setup() {
  //random seed
  randomSeed(analogRead(0));

  // Initialize RP2040
  rp2040.begin();

  // Initialize Serial communication
  Serial.begin(9600);

  // Initialize I2C with custom pins
  Wire.setSDA(SDA_PIN);
  Wire.setSCL(SCL_PIN);
  Wire.begin();

  // Initialize the OLED display
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setRotation(3);
  display.clearDisplay();
}

int Game(int Type, bool UndoRest = false) {
  //Initialize Cells array with 12 items
  CellItem Cells[12] = {
    CellItem(Rock, 2), CellItem(King, 2), CellItem(Bishop, 2),
    CellItem(Empty, 0), CellItem(Pawn, 2), CellItem(Empty, 0),
    CellItem(Empty, 0), CellItem(Pawn, 1), CellItem(Empty, 0),
    CellItem(Bishop, 1), CellItem(King, 1), CellItem(Rock, 1)
  };


  CellItem Inventory[MAX_INVENTORY] = {};
  int PlayerTurn = 1;
  int Winner = -1;

  if (UndoRest) {
    LoadGame();
    std::copy(std::begin(GameSaves.RestState.cells), std::end(GameSaves.RestState.cells), Cells);
    std::copy(std::begin(GameSaves.RestState.inventory), std::end(GameSaves.RestState.inventory), Inventory);
    PlayerTurn = GameSaves.RestState.PlayerTurn;
  }

  auto Draw = [&](bool HideInventory = false) -> void {
    auto GetPieceCharacter = [](PieceType Piece) -> char {
      switch (Piece) {
        case Empty: return ' ';
        case Pawn: return 'P';
        case King: return 'K';
        case Bishop: return 'B';
        case Rock: return 'R';
        case Gold: return 'G';
      }
      return ' ';
    };
    //Clear the display
    display.clearDisplay();

    // Display the current player's turn
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print(F("Player "));
    display.print(PlayerTurn);
    display.print(F(""));

    // Draw the decorative grid
    int startX = 3;       // Starting position for the grid
    int startY = 12;      // Starting position for the grid rows
    int cellHeight = 15;  // Height for each cell
    int cellWidth = 42;

    // Draw the top border of the grid
    display.setCursor(startX, startY - 2);  // Move slightly above the first row
    display.print(F("+-+-+-+"));

    // Loop through each row to draw pieces and separators
    for (int y = 0; y < 4; ++y) {
      display.setCursor(startX, 7.5 + startY + y * cellHeight);  // Adjust position for each row
      display.print(F("|"));                                     // Start row with vertical line

      for (int x = 0; x < 3; ++x) {

        int TempCursorX = display.getCursorX(), TempCursorY = display.getCursorY();

        display.setRotation(Cells[y * 3 + x].TeamID == 1 ? 3 : 1);
        if (Cells[y * 3 + x].TeamID == 2) {
          display.setCursor(64 - (14 + 12 * x), 128 - (14 + 12 + 15 * y));
        }
        display.print(GetPieceCharacter(Cells[y * 3 + x].Piece));  // Draw the piece

        display.setRotation(3);

        display.setCursor(TempCursorX, TempCursorY);
        display.print(F(" |"));  // End cell with vertical line
      }

      // Draw the horizontal separator after each row
      display.setCursor(startX, startY + (y + 1) * cellHeight);  // Adjust for the line after the row
      display.print(F("+-+-+-+"));                               // Draw the horizontal separator
    }

    if (!HideInventory) {
      // Display the inventory
      display.setCursor(0, 80);
      for (int i = 0; i < MAX_INVENTORY; ++i) {
        if (Inventory[i].Piece != Empty) {
          display.print("[");
          display.print(GetPieceCharacter(Inventory[i].Piece));
          display.print(", ");
          display.print(Inventory[i].TeamID);
          display.print("]");
          display.print("\n");
        }
      }
    } else {
      for (int i = 0; i < MAX_INVENTORY; ++i) {
        if (Inventory[i].TeamID == PlayerTurn) {
          display.setTextSize(2);
          display.setCursor(Inventory[i].Piece * 12 - 6, 80);
          display.print(GetPieceCharacter(Inventory[i].Piece));
        }
      }
    }

    display.display();  // Update the display

    /*
    * Debug
    */

    // Serial.println("Board");
    // for (int y = 0; y < 4; ++y) {
    //   for (int x = 0; x < 3; ++x) {
    //     Serial.print("|");
    //     Serial.print(GetPieceCharacter(Cells[y * 3 + x].Piece));
    //     Serial.print(", ");
    //     Serial.print(Cells[y * 3 + x].TeamID);
    //   }
    //   Serial.println("|");
    // }

    // for (int i = 0; i < 6; ++i) {
    //   Serial.print("[");
    //   Serial.print(GetPieceCharacter(Inventory[i].Piece));
    //   Serial.print(", ");
    //   Serial.print(Inventory[i].TeamID);
    //   Serial.println("]");
    // }

    // Debug End
  };

  auto AnimateMove = [&](const int& From, const int& To, const int& steps = 4) -> void {
    char FromPiece = [&]() -> char {
      switch (Cells[From].Piece) {
        case Empty: return ' ';
        case Pawn: return 'P';
        case King: return 'K';
        case Bishop: return 'B';
        case Rock: return 'R';
        case Gold: return 'G';
      }
      return ' ';
    }();

    float posYf = From / 3;
    float posXf = From % 3;
    float DisplayXf = 6 + 12 * posXf;
    float DisplayYf = 16.5 + 15 * posYf;

    float posYt = To / 3;
    float posXt = To % 3;
    float DisplayXt = 6 + 12 * posXt;
    float DisplayYt = 16.5 + 15 * posYt;

    float DirectionX = DisplayXt - DisplayXf;
    float DirectionY = DisplayYt - DisplayYf;
    float Distance = sqrt(pow(DirectionX, 2) + pow(DirectionY, 2));

    Draw(true);

    display.setTextSize(1);

    for (int i = steps; i > 0; --i) {
      display.setCursor(DirectionX / i + DisplayXf, DirectionY / i + DisplayYf);
      display.print(FromPiece);
      display.display();
    }
  };

  auto AnimateInventoryDrop = [&](const int& CellToAnimate, const int& steps = 4) -> void {
    float posY = CellToAnimate / 3;
    float posX = CellToAnimate % 3;
    float DisplayX = 12 / 2 + 6 + 12 * posX;
    float DisplayY = 15 / 2 + 16.5 + 15 * posY;

    Draw(true);

    display.setTextSize(1);

    for (int i = steps; i > 0; --i) {
      display.fillCircle(DisplayX, DisplayY, 7 / i, WHITE);
      display.fillCircle(DisplayX, DisplayY, 7 / (i + 1), BLACK);
      display.display();
    }
  };

  auto FindPiece = [&](const PieceType& Piece, const int& TeamID) -> int {
    for (int i = 0; i < 12; ++i) {
      if (Cells[i].TeamID == TeamID && Cells[i].Piece == Piece) {
        return i;
      }
    }
    return -1;
  };

  auto CanPieceAttack = [&](const int& From, const int& To) -> bool {
    int DeltaX = To % 3 - From % 3, DeltaY = From / 3 - To / 3;

    // Restrict move to a 1-cell radius for all pieces
    if (DeltaX == 0 && DeltaY == 0 || abs(DeltaX) > 1 || abs(DeltaY) > 1) return false;

    switch (Cells[From].Piece) {
      case King:
        // King can move in any direction within 1 cell range
        return true;

      case Rock:
        // Rock can move vertically or horizontally but not diagonally
        return DeltaX == 0 || DeltaY == 0;

      case Bishop:
        // Bishop can move diagonally, so both DeltaX and DeltaY must be non-zero and equal
        return abs(DeltaX) == abs(DeltaY);

      case Gold:
        // Gold moves like a king forward, but can't move backward diagonal
        return DeltaX == 0 ? true : DeltaY != (Cells[From].TeamID == 1 ? -1 : 1);

      case Pawn:
        // Pawn can move forward 1 cell, and attack diagonally forward
        return DeltaX == 0 && DeltaY == (Cells[From].TeamID == 1 ? 1 : -1);
    };
    return false;
  };

  /*
  * Board State
  */

  enum DMT {
    Score,
    LastDirection
  };

  enum PMT {
    From,
    To,
    MAX  // This will still represent the number of elements
  };


  class BoardStateClass {
  public:
    BoardStateClass() {
      AllPossibleMoves = new int*[AllPossibleMovesLength];
      for (int i = 0; i < AllPossibleMovesLength; ++i) {
        AllPossibleMoves[i] = new int[PMT::MAX];
        memset(AllPossibleMoves[i], -1, PMT::MAX * sizeof(int));  // Initialize with -1
      }
    }

    ~BoardStateClass() {
      for (int i = 0; i < AllPossibleMovesLength; ++i) {
        delete[] AllPossibleMoves[i];
      }
      delete[] AllPossibleMoves;
    }

    // Simplified to get all moves without filtering by team
    int GetAllPossibleMovesLength() const {
      return AllPossibleMovesLength;
    }

    int* GetMove(int index) const {
      if (index >= 0 && index < AllPossibleMovesLength) {
        return AllPossibleMoves[index];
      }
      return nullptr;  // Invalid index
    }

    bool IsEmpty() const {
      return AllPossibleMovesLength == 1 && AllPossibleMoves[0][0] == -1;
    }

    bool IsMoveUnset(int* move) {
      return move[From] == -1 && move[To] == -1;
    }

    // Add a new move without considering teamID
    void AddMove(int from, int to) {
      for (int i = 0; i < AllPossibleMovesLength; ++i) {
        if (IsMoveUnset(AllPossibleMoves[i])) {
          // Replace unset item
          AllPossibleMoves[i][From] = from;
          AllPossibleMoves[i][To] = to;
          return;
        }
      }

      // Resize if no unset items found
      int** temp = new int*[AllPossibleMovesLength + 1];
      for (int i = 0; i < AllPossibleMovesLength; ++i) {
        temp[i] = AllPossibleMoves[i];
      }
      temp[AllPossibleMovesLength] = new int[PMT::MAX]{ -1, -1 };  // Initialize all to -1
      temp[AllPossibleMovesLength][PMT::From] = from;              // Assign the From value
      temp[AllPossibleMovesLength][PMT::To] = to;                  // Assign the To value


      delete[] AllPossibleMoves;
      AllPossibleMoves = temp;
      ++AllPossibleMovesLength;
    }

    void Clear() {
      if (IsEmpty()) return;

      delete[] AllPossibleMoves;
      AllPossibleMovesLength = 1;

      AllPossibleMoves = new int*[AllPossibleMovesLength];
      for (int i = 0; i < AllPossibleMovesLength; ++i) {
        AllPossibleMoves[i] = new int[PMT::MAX];
        memset(AllPossibleMoves[i], -1, PMT::MAX * sizeof(int));  // Initialize with -1
      }
    }

  private:
    int AllPossibleMovesLength = 1;  // Initial length
    int** AllPossibleMoves;
  };

  BoardStateClass BoardState;

  auto EvaluateBoard = [&]() -> bool {
    BoardState.Clear();
    int DangerousMap[12][2] = { 0 };

    for (int CellFrom = 0; CellFrom < 12; ++CellFrom) {
      for (int CellTo = 0; CellTo < 12; ++CellTo) {
        if (Cells[CellFrom].TeamID == (PlayerTurn == 1 ? 2 : 1) && CanPieceAttack(CellFrom, CellTo)) {
          DangerousMap[CellTo][DMT::Score]++;
          DangerousMap[CellTo][DMT::LastDirection] = CellFrom;
        }
      }
    }

    int PlayerKingIndex = FindPiece(King, PlayerTurn);
    bool bKingCanBeCaptured = DangerousMap[PlayerKingIndex][DMT::Score] > 0;

    if (bKingCanBeCaptured) {
      //King safe moves handle
      if (DangerousMap[PlayerKingIndex][DMT::Score == 1]) {
        for (int PlayerPiece = 0; PlayerPiece < 12; ++PlayerPiece) {
          if (Cells[PlayerPiece].TeamID == PlayerTurn && PlayerPiece != PlayerKingIndex && CanPieceAttack(PlayerPiece, DangerousMap[PlayerKingIndex][DMT::LastDirection])) {
            BoardState.AddMove(PlayerPiece, DangerousMap[PlayerKingIndex][DMT::LastDirection]);
          }
        }
      }

      for (int SafeCell = 0; SafeCell < 12; ++SafeCell) {
        if (DangerousMap[SafeCell][DMT::Score] == 0 && Cells[SafeCell].TeamID != PlayerTurn && CanPieceAttack(PlayerKingIndex, SafeCell)) {
          BoardState.AddMove(PlayerKingIndex, SafeCell);
        }
      }

    } else {
      // Defualt moves
      for (int CellFrom = 0; CellFrom < 12; ++CellFrom) {
        for (int CellTo = 0; CellTo < 12; ++CellTo) {
          if (Cells[CellFrom].TeamID == PlayerTurn && Cells[CellTo].TeamID != PlayerTurn && CanPieceAttack(CellFrom, CellTo)) {
            if (CellFrom == PlayerKingIndex && DangerousMap[CellTo][DMT::Score] > 0) continue;
            BoardState.AddMove(CellFrom, CellTo);
          }
        }
      }

      // Inventory moves
      for (int EmptyCell = 0; EmptyCell < 12; ++EmptyCell) {
        if (Cells[EmptyCell].Piece == Empty) {
          for (int i = 0; i < MAX_INVENTORY; ++i) {
            if (Inventory[i].TeamID == PlayerTurn) {
              int InventoryCell = -1;
              switch (Inventory[i].Piece) {
                case Pawn: InventoryCell = 12; break;
                case Rock: InventoryCell = 13; break;
                case Bishop: InventoryCell = 14; break;
              }
              if (InventoryCell != -1) {
                BoardState.AddMove(InventoryCell, EmptyCell);
              }
            }
          }
        }
      }
    }

    return DangerousMap[PlayerKingIndex][DMT::Score] > 0;
  };

  auto CanMovePieceTo = [&](int From, int To = -1) -> bool {
    for (int i = 0; i < BoardState.GetAllPossibleMovesLength(); ++i) {
      const auto TempMove = BoardState.GetMove(i);
      if (TempMove[PMT::From] == From && (To == -1 ? true : TempMove[PMT::To] == To)) {
        return true;
      }
    }
    return false;
  };

  auto BotInput = [&](int& First, int& Second) -> void {
    const auto Move = BoardState.GetMove(random() % BoardState.GetAllPossibleMovesLength());
    First = Move[PMT::From];
    Second = Move[PMT::To];
  };

  auto PlayerInput = [&](int& First, int& Second) -> void {
    First = Second = -1;

    int CurrentNumber = 11;
    auto ChangeNumberBy = [&](int Delta) -> int {
      for (int i = 0; i < 15; ++i) {
        CurrentNumber++;
        CurrentNumber = CurrentNumber % 15;

        if (First == -1) {
          if (CanMovePieceTo(CurrentNumber)) return CurrentNumber;
        } else if (Second == -1) {
          if (CanMovePieceTo(First, CurrentNumber) || CurrentNumber == First) return CurrentNumber;
        }
      }
      return CurrentNumber;
    };
    while ((First == -1 || Second == -1) || (First == Second)) {
      if (First == Second) {
        First = Second = -1;
      }
      for (int i = 0; i < 20; ++i) {
        sleep_ms(50);
        if (BOOTSEL) {
          if (First == -1) {
            First = CurrentNumber;
            while (BOOTSEL)
              ;
            break;
          } else if (Second == -1) {
            Second = CurrentNumber;
            while (BOOTSEL)
              ;
            break;
          }
        }
      }

      ChangeNumberBy(PlayerTurn - 1);
      Draw(CurrentNumber > 11 || First > 11);

      int posY = CurrentNumber / 3, posX = CurrentNumber % 3;
      display.setCursor(6 + 12 * posX, 16.5 + 15 * posY + (CurrentNumber > 11 ? 20 : 0));
      display.setTextSize(2);
      if (CurrentNumber > 11 && First == -1)
        display.print(F("^"));
      else
        display.print(F("X"));

      if (First != -1) {
        posY = First / 3;
        posX = First % 3;

        display.setCursor(6 + 12 * posX, 16.5 + 15 * posY + (First > 11 ? 20 : 0));
        if (First > 11)
          display.print(F("^"));
        else
          display.print(F("X"));
      }

      display.display();
    }
  };


  while (Winner == -1) {
    bool bCanKingBeCaptured = EvaluateBoard();
    if (BoardState.IsEmpty()) {
      if (bCanKingBeCaptured) {
        Winner = PlayerTurn == 1 ? 2 : 1;
      } else {
        Winner = 0;
      }
      continue;
    }

    Draw();

    /*
    * Save Game State
    */

    GameSaves.bPlayerCaused = true;
    GameSaves.gameType = Type;
    std::copy(std::begin(PlayerScore), std::end(PlayerScore), GameSaves.playerScore);

    GameSaves.RestState.PlayerTurn = PlayerTurn;
    std::copy(std::begin(Cells), std::end(Cells), GameSaves.RestState.cells);
    std::copy(std::begin(Inventory), std::end(Inventory), GameSaves.RestState.inventory);

    SaveGame();

    // end saving game state

    // Move
    [&]() -> void {
      int From = 0, To = 0;


      switch (Type) {
        case 1: PlayerInput(From, To); break;

        case 2:
          if (PlayerTurn == 1)
            PlayerInput(From, To);
          else
            BotInput(From, To);
          break;

        case 3: BotInput(From, To); break;
      };


      if (Cells[From].TeamID != PlayerTurn && Cells[To].TeamID == PlayerTurn || To > 11 && To < 15 || Cells[To].TeamID == PlayerTurn && Cells[From].TeamID == 0) {
        int Temp = From;
        From = To;
        To = Temp;
      }

      /*
      * Inventory move
      */
      if (From > 11 && From < 15) {
        PieceType SearchingFor = Empty;

        switch (From) {
          case 12: SearchingFor = Pawn; break;
          case 13: SearchingFor = Rock; break;
          case 14: SearchingFor = Bishop; break;
        }

        for (int i = 0; i < 6; ++i) {
          if (Inventory[i].Piece == SearchingFor && Inventory[i].TeamID == PlayerTurn) {
            if (Cells[To].Piece == Empty) {
              CellItem::swap(Inventory[i], Cells[To]);
              PlayerTurn = PlayerTurn == 1 ? 2 : 1;
              AnimateInventoryDrop(To);
              return;
            }
          }
        }
      }

      /*
      * Move
      */
      if (CanMovePieceTo(From, To)) {

        // Serial.print("From = ");
        // Serial.print(From);
        // Serial.print(", To = ");
        // Serial.println(To);

        if (Cells[To].TeamID != PlayerTurn) {
          for (int i = 0; i < 6; ++i) {
            if (Inventory[i].Piece == Empty) {
              // Swap the captured piece with an empty inventory slot
              CellItem::swapInventory(Inventory[i], Cells[To]);
              break;  // Exit after capturing and swapping
            }
          }
        }

        AnimateMove(From, To);

        CellItem::swap(Cells[From], Cells[To]);

        if (PlayerTurn - 1 ? To / 3 == 3 : To / 3 == 0) {
          switch (Cells[To].Piece) {
            // King rising end of the board
            case King: Winner = PlayerTurn; break;
            // Pawn rising end of the board
            case Pawn: Cells[To].Piece = Gold; break;
          }
        }

        PlayerTurn = PlayerTurn == 1 ? 2 : 1;
      }
    }();
  }

  Draw();

  return Winner;
}

void loop() {
  // Load the game state if available
  bool bPlayerConfirmRest = true;
  if (LoadGame()) {
    GameType = GameSaves.gameType;
    std::copy(std::begin(GameSaves.playerScore), std::end(GameSaves.playerScore), PlayerScore);

    if (GameSaves.bPlayerCaused && GameSaves.gameType != -1) {
      int bPlayerConfirm = -1;
      for (int CurrentSolution = 1; bPlayerConfirm == -1; CurrentSolution = CurrentSolution == 1 ? 0 : 1) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);

        if (CurrentSolution) {
          display.print(F("Are\nyou\nsure?\n\n|YES|\n\n NO"));
        } else {
          display.print(F("Are\nyou\nsure?\n\n YES\n\n|NO |"));
        }

        display.display();

        for (int i = 0; i < 20; ++i) {
          sleep_ms(50);
          if (BOOTSEL) {
            bPlayerConfirm = CurrentSolution;
            while (BOOTSEL)
              ;
            break;
          }
        }
      }
      bPlayerConfirmRest = bPlayerConfirm;
      if(bPlayerConfirm){
        GameType = -1;
      }
    }
  }
  GameSaves.gameType = -1;
  for (int i = 0; i < 3; ++i) {
    GameSaves.playerScore[i] = 0;
  }
  GameSaves.bPlayerCaused = true;
  SaveGame();

  for (int CurrentType = 1; GameType == -1; CurrentType = CurrentType % 3 + 1) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 20);

    for (int i = 1; i <= 3; ++i) {
      if (CurrentType == i)
        display.print("-");
      else
        display.print(" ");

      display.print(" ");
      display.println([](int index) -> const char* {
        switch (index) {
          case 1: return "PvP";
          case 2: return "PvB";
          case 3: return "BvB";
        };
        return " ";
      }(i));
      display.println("");
    }

    display.display();

    for (int i = 0; i < 20; ++i) {
      sleep_ms(50);
      if (BOOTSEL) {
        GameType = CurrentType;
        while (BOOTSEL)
          ;
        break;
      }
    }
  }

  if(GameType < 1 || GameType > 3) return;

  int Winner = Game(GameType, !bPlayerConfirmRest);


  if (Winner >= 0 && Winner < 3) {
    PlayerScore[Winner]++;
  }

  if (GameType != 3) {
    display.setTextSize(10);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(4, 22);
    if (Winner == 0) {
      display.print("=");
    } else {
      display.print(Winner);
    }
    display.display();

    sleep_ms(1000);

    // Simple animation: expand a filled rectangle from the center of the screen
    int centerX = 64 / 2;
    int centerY = 128 / 2;
    int rectSize = 0;  // Start size for the rectangle

    while (rectSize < max(SCREEN_WIDTH, SCREEN_HEIGHT) + 100) {

      // Draw expanding rectangle from the center
      display.fillCircle(centerX, centerY, rectSize, SSD1306_WHITE);
      display.display();

      rectSize += 4;  // Increase the rectangle size on each iteration

      sleep_ms(50);  // Small delay for animation smoothness
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 20);
    display.println("ScoreBoard");
    display.print("\n\n");
    display.print("Player1:");
    display.println(PlayerScore[1]);
    display.print("\n");
    display.print("Player2:");
    display.println(PlayerScore[2]);
    display.print("\n");
    display.print(" Draws:");
    display.println(PlayerScore[0]);
    display.display();

    sleep_ms(2500);
  }

  // Save game state before restarting
  GameSaves.gameType = GameType;
  std::copy(std::begin(PlayerScore), std::end(PlayerScore), GameSaves.playerScore);
  GameSaves.bPlayerCaused = false;
  SaveGame();
  rp2040.restart();
}
